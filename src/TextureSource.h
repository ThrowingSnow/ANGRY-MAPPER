#pragma once
#include <GL/glew.h>
#include <string>
#include <vector>

class TextureSource {
public:
    virtual ~TextureSource() = default;

    virtual bool connect(const std::string& sourceName) = 0;
    virtual void disconnect() = 0;
    virtual bool update() = 0;  // returns true if new frame available

    virtual GLuint texture() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual bool isConnected() const = 0;

    virtual std::vector<std::string> listSources() = 0;
};
