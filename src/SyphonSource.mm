#ifdef __APPLE__
#include "SyphonSource.h"
#import  <Syphon/Syphon.h>
#import  <OpenGL/OpenGL.h>   // CGLGetCurrentContext
#include <cstdio>

// ---- PIMPL ----
struct SyphonSource::Impl {
    // Syphon objects (nil-safe thanks to ARC)
    SyphonOpenGLClient* __strong client   = nil;

    // GL resources for RECT→2D blit (allocated on first RECTANGLE frame)
    GLuint texID    = 0;
    GLuint readFBO  = 0;
    GLuint drawFBO  = 0;
    int    w = 0, h = 0;
    bool   connected = false;

    // Blit a GL_TEXTURE_RECTANGLE srcTex into our internal GL_TEXTURE_2D.
    // Both FBOs are reused across frames; texture is resized if dimensions change.
    void blitRectTo2D(GLuint srcTex, int newW, int newH) {
        if (newW != w || newH != h || !texID) {
            if (texID) glDeleteTextures(1, &texID);
            glGenTextures(1, &texID);
            glBindTexture(GL_TEXTURE_2D, texID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, newW, newH,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
            w = newW; h = newH;
        }
        if (!readFBO) glGenFramebuffers(1, &readFBO);
        if (!drawFBO) glGenFramebuffers(1, &drawFBO);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_RECTANGLE, srcTex, 0);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFBO);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, texID, 0);

        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    void cleanup() {
        if (texID)   { glDeleteTextures(1, &texID);         texID   = 0; }
        if (readFBO) { glDeleteFramebuffers(1, &readFBO);   readFBO = 0; }
        if (drawFBO) { glDeleteFramebuffers(1, &drawFBO);   drawFBO = 0; }
        w = h = 0;
    }
};

// ---- SyphonSource ----

SyphonSource::SyphonSource()
    : m_impl(std::make_unique<Impl>()) {}

SyphonSource::~SyphonSource() {
    disconnect();
}

std::vector<std::string> SyphonSource::listSources() {
    std::vector<std::string> result;
    NSArray* servers = [[SyphonServerDirectory sharedDirectory] servers];
    for (NSDictionary* desc in servers) {
        NSString* appName  = desc[SyphonServerDescriptionAppNameKey]  ?: @"?";
        NSString* srvName  = desc[SyphonServerDescriptionNameKey]     ?: @"?";
        NSString* combined = [NSString stringWithFormat:@"%@ — %@", appName, srvName];
        result.push_back([combined UTF8String]);
    }
    return result;
}

bool SyphonSource::connect(const std::string& serverName) {
    disconnect();

    NSArray* servers = [[SyphonServerDirectory sharedDirectory] servers];
    NSDictionary* targetDesc = nil;
    for (NSDictionary* desc in servers) {
        NSString* appName  = desc[SyphonServerDescriptionAppNameKey]  ?: @"?";
        NSString* srvName  = desc[SyphonServerDescriptionNameKey]     ?: @"?";
        NSString* combined = [NSString stringWithFormat:@"%@ — %@", appName, srvName];
        if (std::string([combined UTF8String]) == serverName) {
            targetDesc = desc;
            break;
        }
    }
    if (!targetDesc) return false;

    CGLContextObj cglCtx = CGLGetCurrentContext();
    if (!cglCtx) {
        fprintf(stderr, "SyphonSource: no current CGL context — call connect() on the GL thread\n");
        return false;
    }

    m_impl->client = [[SyphonOpenGLClient alloc]
                         initWithServerDescription:targetDesc
                                           context:cglCtx
                                           options:nil
                                   newFrameHandler:nil];

    m_impl->connected = (m_impl->client != nil && [m_impl->client isValid]);
    return m_impl->connected;
}

void SyphonSource::disconnect() {
    if (m_impl->client) {
        [m_impl->client stop];
        m_impl->client = nil;
    }
    m_impl->cleanup();
    m_impl->connected = false;
}

void SyphonSource::update() {
    if (!m_impl->client || ![m_impl->client isValid]) {
        m_impl->connected = false;
        return;
    }

    @autoreleasepool {
        SyphonOpenGLImage* frame = [m_impl->client newFrameImage];
        if (!frame) return;

        NSSize sz   = frame.textureSize;
        GLuint src  = (GLuint)frame.textureName;
        GLenum tgt  = (GLenum)frame.textureTarget;
        int    w    = (int)sz.width;
        int    h    = (int)sz.height;

        if (tgt == GL_TEXTURE_2D) {
            // Zero-copy path: use Syphon's texture directly
            m_impl->texID = src;
            m_impl->w = w;
            m_impl->h = h;
        } else {
            // GL_TEXTURE_RECTANGLE → blit into internal GL_TEXTURE_2D
            m_impl->blitRectTo2D(src, w, h);
        }
    }
}

GLuint SyphonSource::texture()     const { return m_impl->texID; }
int    SyphonSource::width()       const { return m_impl->w; }
int    SyphonSource::height()      const { return m_impl->h; }
bool   SyphonSource::isConnected() const {
    return m_impl->connected && m_impl->client && [m_impl->client isValid];
}

#endif // __APPLE__
