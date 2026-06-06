#include "NDISource.h"
#include <Processing.NDI.Lib.h>
#include <stdexcept>
#include <chrono>

NDISource::NDISource() {
    if (!NDIlib_initialize())
        throw std::runtime_error("NDI initialization failed");

    m_find = NDIlib_find_create_v2();
}

NDISource::~NDISource() {
    disconnect();
    if (m_find) NDIlib_find_destroy(m_find);
    if (m_texture) glDeleteTextures(1, &m_texture);
    NDIlib_destroy();
}

std::vector<std::string> NDISource::listSources() {
    // Give the finder 1 second to discover sources on the network
    NDIlib_find_wait_for_sources(m_find, 1000);

    uint32_t count = 0;
    const NDIlib_source_t* sources = NDIlib_find_get_current_sources(m_find, &count);

    std::vector<std::string> names;
    names.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        names.emplace_back(sources[i].p_ndi_name);

    return names;
}

bool NDISource::connect(const std::string& sourceName) {
    disconnect();

    NDIlib_find_wait_for_sources(m_find, 1000);
    uint32_t count = 0;
    const NDIlib_source_t* sources = NDIlib_find_get_current_sources(m_find, &count);

    const NDIlib_source_t* target = nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        if (sourceName == sources[i].p_ndi_name) {
            target = &sources[i];
            break;
        }
    }

    if (!target) return false;

    NDIlib_recv_create_v3_t recvDesc;
    recvDesc.source_to_connect_to = *target;
    recvDesc.color_format = NDIlib_recv_color_format_RGBX_RGBA;
    recvDesc.bandwidth = NDIlib_recv_bandwidth_highest;
    recvDesc.allow_video_fields = false;

    m_recv = NDIlib_recv_create_v3(&recvDesc);
    return m_recv != nullptr;
}

void NDISource::disconnect() {
    if (m_recv) {
        NDIlib_recv_destroy(m_recv);
        m_recv = nullptr;
    }
}

bool NDISource::update() {
    if (!m_recv) return false;

    NDIlib_video_frame_v2_t videoFrame;
    auto result = NDIlib_recv_capture_v3(m_recv, &videoFrame, nullptr, nullptr, 0);

    if (result == NDIlib_frame_type_video) {
        uploadFrame(videoFrame);
        NDIlib_recv_free_video_v2(m_recv, &videoFrame);
        return true;
    }

    return false;
}

void NDISource::uploadFrame(const NDIlib_video_frame_v2_t& frame) {
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

    m_width  = frame.xres;
    m_height = frame.yres;

    // NDI gives us RGBA when color_format is RGBX_RGBA
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 m_width, m_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE,
                 frame.p_data);

    glBindTexture(GL_TEXTURE_2D, 0);
}
