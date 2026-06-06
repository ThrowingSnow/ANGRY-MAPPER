#pragma once
#include "TextureSource.h"
#include <Processing.NDI.Lib.h>

class NDISource : public TextureSource {
public:
    NDISource();
    ~NDISource() override;

    bool connect(const std::string& sourceName) override;
    void disconnect() override;
    bool update() override;

    GLuint texture() const override { return m_texture; }
    int width() const override { return m_width; }
    int height() const override { return m_height; }
    bool isConnected() const override { return m_recv != nullptr; }

    std::vector<std::string> listSources() override;

private:
    void uploadFrame(const NDIlib_video_frame_v2_t& frame);

    NDIlib_find_instance_t m_find = nullptr;
    NDIlib_recv_instance_t m_recv = nullptr;
    GLuint m_texture = 0;
    int m_width = 0;
    int m_height = 0;
};
