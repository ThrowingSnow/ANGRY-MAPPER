#pragma once

#include "TextureSource.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

class PipeWireSource : public TextureSource {
public:
    enum class State { Idle, WaitingForPortal, Streaming, Error };

    PipeWireSource();
    ~PipeWireSource() override;

    // Opens the KDE window/screen picker dialog
    void requestCapture();

    // TextureSource interface
    bool connect(const std::string&) override { return false; }
    void disconnect() override;
    bool update() override;

    GLuint texture() const override { return m_texture; }
    int width()      const override { return m_width; }
    int height()     const override { return m_height; }
    bool isConnected() const override { return m_state == State::Streaming; }
    std::vector<std::string> listSources() override { return {}; }

    State state() const { return m_state.load(); }
    const char* stateStr() const;

    // Called by free callback functions in the .cpp (internal use)
    void _onSessionReady(void* session, void* loop);
    void _onSessionStarted(void* session, void* loop);
    void _onPortalError(void* loop);
    void _onPwFrame(const uint8_t* data, int w, int h, uint32_t format, uint32_t stride);
    void _setNegotiatedSize(int w, int h, uint32_t fmt);

private:
    void connectPipeWire(int fd, uint32_t nodeId);

    // Opaque GLib / libportal handles (avoid pulling GLib into every TU)
    void* m_glibThread = nullptr;  // GThread*
    void* m_session    = nullptr;  // XdpSession*

    // Opaque PipeWire handles
    void* m_pwLoop     = nullptr;  // pw_thread_loop*
    void* m_pwContext  = nullptr;  // pw_context*
    void* m_pwCore     = nullptr;  // pw_core*
    void* m_pwStream   = nullptr;  // pw_stream*
    void* m_streamHook = nullptr;  // spa_hook* (heap-allocated)

    // Negotiated stream properties (written from PW thread, read from update())
    int      m_streamW   = 0;
    int      m_streamH   = 0;
    uint32_t m_streamFmt = 0;  // SPA_VIDEO_FORMAT_*

    // Frame buffer shared between PW thread and main thread
    std::mutex           m_frameMutex;
    std::vector<uint8_t> m_pendingData;
    int      m_pendingW      = 0;
    int      m_pendingH      = 0;
    uint32_t m_pendingFormat = 0;
    bool     m_newFrame      = false;

    // GL state — main thread only
    GLuint m_texture = 0;
    int    m_width   = 0;
    int    m_height  = 0;

    std::atomic<State> m_state{State::Idle};
};
