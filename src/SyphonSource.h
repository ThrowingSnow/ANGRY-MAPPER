#pragma once
#ifdef __APPLE__

#include <GL/glew.h>   // must match the GL header main.cpp uses
#include <string>
#include <vector>
#include <memory>

// Receives frames from a Syphon server (macOS).
// Handles both GL_TEXTURE_2D and GL_TEXTURE_RECTANGLE outputs
// by blitting RECTANGLE into an internal GL_TEXTURE_2D via FBO.
class SyphonSource {
public:
    SyphonSource();
    ~SyphonSource();

    std::vector<std::string> listSources();
    bool connect(const std::string& serverName);
    void disconnect();
    void update();   // call once per frame on the GL thread

    GLuint texture()     const;
    int    width()       const;
    int    height()      const;
    bool   isConnected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // __APPLE__
