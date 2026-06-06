#include "PipeWireSource.h"

// GLib / libportal
#include <glib.h>
#include <gio/gio.h>
#include <libportal/portal.h>
#include <libportal/remote.h>

// PipeWire
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/props.h>

// std
#include <cstdio>
#include <cstring>
#include <stdexcept>

// ============================================================
// Helpers
// ============================================================

static GLenum spaFormatToGL(uint32_t fmt) {
    switch (fmt) {
    case SPA_VIDEO_FORMAT_RGBA:
    case SPA_VIDEO_FORMAT_RGBx:
        return GL_RGBA;
    case SPA_VIDEO_FORMAT_BGRA:
    case SPA_VIDEO_FORMAT_BGRx:
        return GL_BGRA;
    default:
        return GL_RGBA;
    }
}

// ============================================================
// GLib thread + portal callbacks
// ============================================================

struct PortalCtx {
    PipeWireSource* src;
};

static void sessionStartedCB(GObject* obj, GAsyncResult* res, gpointer data) {
    auto* ctx = static_cast<PortalCtx*>(data);
    GError* err = nullptr;

    if (!xdp_session_start_finish(XDP_SESSION(obj), res, &err)) {
        fprintf(stderr, "[portal] session start failed: %s\n",
                err ? err->message : "unknown");
        if (err) g_error_free(err);
        ctx->src->_onPortalError(nullptr);
        return;
    }

    ctx->src->_onSessionStarted(XDP_SESSION(obj), nullptr);
}

static void sessionCreatedCB(GObject* obj, GAsyncResult* res, gpointer data) {
    auto* ctx = static_cast<PortalCtx*>(data);
    GError* err = nullptr;

    XdpSession* session = xdp_portal_create_screencast_session_finish(
        XDP_PORTAL(obj), res, &err);

    if (!session) {
        fprintf(stderr, "[portal] create session failed: %s\n",
                err ? err->message : "unknown");
        if (err) g_error_free(err);
        ctx->src->_onPortalError(nullptr);
        return;
    }

    ctx->src->_onSessionReady(session, nullptr);

    xdp_session_start(session, nullptr, nullptr, sessionStartedCB, data);
}

static gpointer glibThreadFunc(gpointer data) {
    auto* ctx = static_cast<PortalCtx*>(data);

    GMainContext* mainCtx = g_main_context_new();
    g_main_context_push_thread_default(mainCtx);
    GMainLoop* loop = g_main_loop_new(mainCtx, FALSE);

    XdpPortal* portal = xdp_portal_new();

    xdp_portal_create_screencast_session(
        portal,
        (XdpOutputType)(XDP_OUTPUT_WINDOW | XDP_OUTPUT_MONITOR),
        XDP_SCREENCAST_FLAG_NONE,
        XDP_CURSOR_MODE_HIDDEN,
        XDP_PERSIST_MODE_NONE,
        nullptr, nullptr,
        sessionCreatedCB,
        ctx);

    g_main_loop_run(loop);

    g_object_unref(portal);
    g_main_loop_unref(loop);
    g_main_context_pop_thread_default(mainCtx);
    g_main_context_unref(mainCtx);
    delete ctx;

    return nullptr;
}

// ============================================================
// PipeWire stream callbacks
// ============================================================

struct PwCtx {
    PipeWireSource* src;
    pw_stream*      stream = nullptr;
};

static void pwParamChangedCB(void* data, uint32_t id, const struct spa_pod* param) {
    auto* ctx = static_cast<PwCtx*>(data);
    if (!param || id != SPA_PARAM_Format) return;

    struct spa_video_info info = {};
    if (spa_format_parse(param, &info.media_type, &info.media_subtype) < 0) return;
    if (info.media_type != SPA_MEDIA_TYPE_video ||
        info.media_subtype != SPA_MEDIA_SUBTYPE_raw) return;

    spa_format_video_raw_parse(param, &info.info.raw);

    int w = (int)info.info.raw.size.width;
    int h = (int)info.info.raw.size.height;
    uint32_t fmt = info.info.raw.format;

    ctx->src->_setNegotiatedSize(w, h, fmt);

    // Update buffer parameters so PipeWire allocates correctly-sized buffers
    auto* stream = ctx->stream;
    uint8_t buf[4096];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    const struct spa_pod* params[1];
    params[0] = (struct spa_pod*)spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 32),
        SPA_PARAM_BUFFERS_blocks,  SPA_POD_Int(1),
        SPA_PARAM_BUFFERS_size,    SPA_POD_Int(w * h * 4),
        SPA_PARAM_BUFFERS_stride,  SPA_POD_Int(w * 4));
    pw_stream_update_params(stream, params, 1);
}

static void pwProcessCB(void* data) {
    auto* ctx = static_cast<PwCtx*>(data);
    auto* stream = ctx->stream;

    struct pw_buffer* b = pw_stream_dequeue_buffer(stream);
    if (!b) return;

    struct spa_buffer* spaBuf = b->buffer;
    if (spaBuf->n_datas == 0) {
        pw_stream_queue_buffer(stream, b);
        return;
    }

    void* ptr = spaBuf->datas[0].data;
    uint32_t size = spaBuf->datas[0].chunk->size;
    uint32_t stride = spaBuf->datas[0].chunk->stride;

    if (ptr && size > 0) {
        // Get negotiated dimensions from source
        int w = 0, h = 0;
        uint32_t fmt = SPA_VIDEO_FORMAT_RGBA;
        ctx->src->_setNegotiatedSize(-1, -1, 0);  // read-only probe
        // We pass what we have; _onPwFrame will use the stored negotiated size
        ctx->src->_onPwFrame(
            static_cast<const uint8_t*>(ptr),
            stride, // pass stride as w; _onPwFrame handles it
            0,
            fmt,
            stride);
    }

    pw_stream_queue_buffer(stream, b);
}

// ============================================================
// PipeWireSource implementation
// ============================================================

PipeWireSource::PipeWireSource() {
    pw_init(nullptr, nullptr);
}

PipeWireSource::~PipeWireSource() {
    disconnect();
    if (m_texture) glDeleteTextures(1, &m_texture);
    pw_deinit();
}

void PipeWireSource::requestCapture() {
    if (m_state != State::Idle && m_state != State::Error) return;
    m_state = State::WaitingForPortal;

    auto* ctx = new PortalCtx{this};
    m_glibThread = g_thread_new("portal", glibThreadFunc, ctx);
}

void PipeWireSource::_onSessionReady(void* session, void* /*loop*/) {
    m_session = session;
}

void PipeWireSource::_onSessionStarted(void* sessionPtr, void* /*loop*/) {
    auto* session = static_cast<XdpSession*>(sessionPtr);

    GVariant* streams = xdp_session_get_streams(session);
    if (!streams) {
        fprintf(stderr, "[portal] no streams returned\n");
        _onPortalError(nullptr);
        return;
    }

    uint32_t nodeId = 0;
    GVariantIter iter;
    g_variant_iter_init(&iter, streams);
    GVariant* item;
    if ((item = g_variant_iter_next_value(&iter))) {
        uint32_t id;
        GVariant* props;
        g_variant_get(item, "(u@a{sv})", &id, &props);
        nodeId = id;
        g_variant_unref(props);
        g_variant_unref(item);
    }

    int fd = xdp_session_open_pipewire_remote(session);
    if (fd < 0 || nodeId == 0) {
        fprintf(stderr, "[portal] invalid fd=%d nodeId=%u\n", fd, nodeId);
        _onPortalError(nullptr);
        return;
    }

    printf("[portal] got nodeId=%u fd=%d\n", nodeId, fd);
    connectPipeWire(fd, nodeId);
}

void PipeWireSource::_onPortalError(void* /*loop*/) {
    m_state = State::Error;
}

void PipeWireSource::_setNegotiatedSize(int w, int h, uint32_t fmt) {
    if (w == -1) return;  // probe call, ignore
    m_streamW   = w;
    m_streamH   = h;
    m_streamFmt = fmt;
}

void PipeWireSource::_onPwFrame(const uint8_t* data, int strideOrW, int /*unused*/,
                                 uint32_t /*fmt*/, uint32_t stride) {
    int w = m_streamW;
    int h = m_streamH;
    if (w == 0 || h == 0 || !data) return;

    std::lock_guard<std::mutex> lock(m_frameMutex);
    size_t needed = (size_t)(stride > 0 ? stride : w * 4) * h;
    m_pendingData.resize(needed);
    memcpy(m_pendingData.data(), data, needed);
    m_pendingW      = w;
    m_pendingH      = h;
    m_pendingFormat = m_streamFmt;
    m_newFrame      = true;
}

void PipeWireSource::connectPipeWire(int fd, uint32_t nodeId) {
    auto* loop = pw_thread_loop_new("angry-mapper-pw", nullptr);
    m_pwLoop = loop;

    pw_thread_loop_lock(loop);

    auto* ctx = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    m_pwContext = ctx;

    auto* core = pw_context_connect_fd(ctx, fd, nullptr, 0);
    if (!core) {
        fprintf(stderr, "[pw] connect_fd failed\n");
        pw_thread_loop_unlock(loop);
        m_state = State::Error;
        return;
    }
    m_pwCore = core;

    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE,     "Video",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE,     "Screen",
        nullptr);

    auto* stream = pw_stream_new(core, "angry-mapper", props);
    m_pwStream = stream;

    auto* pwCtx = new PwCtx{this, stream};

    auto* hook = new spa_hook{};
    m_streamHook = hook;

    static const pw_stream_events streamEvents = {
        .version      = PW_VERSION_STREAM_EVENTS,
        .param_changed = pwParamChangedCB,
        .process      = pwProcessCB,
    };
    pw_stream_add_listener(stream, hook, &streamEvents, pwCtx);

    // Build format request
    uint8_t buf[4096];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));

    struct spa_rectangle defSz = {1920, 1080};
    struct spa_rectangle minSz = {1, 1};
    struct spa_rectangle maxSz = {7680, 4320};
    struct spa_fraction defFps = {60, 1};
    struct spa_fraction minFps = {0, 1};
    struct spa_fraction maxFps = {240, 1};

    const struct spa_pod* params[1];
    params[0] = (struct spa_pod*)spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType,       SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype,    SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format,    SPA_POD_CHOICE_ENUM_Id(4,
            SPA_VIDEO_FORMAT_RGBA,
            SPA_VIDEO_FORMAT_RGBA,
            SPA_VIDEO_FORMAT_RGBx,
            SPA_VIDEO_FORMAT_BGRx),
        SPA_FORMAT_VIDEO_size,      SPA_POD_CHOICE_RANGE_Rectangle(
            &defSz, &minSz, &maxSz),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(
            &defFps, &minFps, &maxFps));

    pw_stream_connect(stream,
        PW_DIRECTION_INPUT, nodeId,
        (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
        params, 1);

    pw_thread_loop_unlock(loop);
    pw_thread_loop_start(loop);

    m_state = State::Streaming;
}

void PipeWireSource::disconnect() {
    if (m_pwLoop) {
        pw_thread_loop_stop(static_cast<pw_thread_loop*>(m_pwLoop));
    }
    if (m_pwStream) {
        pw_stream_destroy(static_cast<pw_stream*>(m_pwStream));
        m_pwStream = nullptr;
    }
    if (m_streamHook) {
        spa_hook_remove(static_cast<spa_hook*>(m_streamHook));
        delete static_cast<spa_hook*>(m_streamHook);
        m_streamHook = nullptr;
    }
    if (m_pwCore) {
        pw_core_disconnect(static_cast<pw_core*>(m_pwCore));
        m_pwCore = nullptr;
    }
    if (m_pwContext) {
        pw_context_destroy(static_cast<pw_context*>(m_pwContext));
        m_pwContext = nullptr;
    }
    if (m_pwLoop) {
        pw_thread_loop_destroy(static_cast<pw_thread_loop*>(m_pwLoop));
        m_pwLoop = nullptr;
    }
    if (m_glibThread) {
        g_thread_join(static_cast<GThread*>(m_glibThread));
        m_glibThread = nullptr;
    }
    m_state = State::Idle;
}

bool PipeWireSource::update() {
    std::unique_lock<std::mutex> lock(m_frameMutex);
    if (!m_newFrame) return false;

    int w      = m_pendingW;
    int h      = m_pendingH;
    uint32_t fmt = m_pendingFormat;
    auto data  = std::move(m_pendingData);
    m_newFrame = false;
    lock.unlock();

    if (w == 0 || h == 0 || data.empty()) return false;

    if (!m_texture) {
        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, m_texture);
    }

    m_width  = w;
    m_height = h;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 w, h, 0,
                 spaFormatToGL(fmt), GL_UNSIGNED_BYTE,
                 data.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

const char* PipeWireSource::stateStr() const {
    switch (m_state.load()) {
    case State::Idle:             return "Idle";
    case State::WaitingForPortal: return "Waiting for portal...";
    case State::Streaming:        return "Streaming";
    case State::Error:            return "Error";
    }
    return "?";
}
