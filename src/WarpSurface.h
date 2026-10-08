#pragma once
#include "GL.h"
#include <array>
#include <vector>
#include "Mask.h"

struct WarpPt { float x, y; };  // normalized [0,1] in viewport space

struct ColorAdj {
    float brightness = 0.0f;   // additive,  range [-1, +1]
    float contrast   = 1.0f;   // multiplier, range [0, 4]
    float gamma      = 1.0f;   // exponent,   range [0.1, 4]
};

struct EdgeBlend {
    float left   = 0.0f;   // fade width as fraction of image [0..1]
    float right  = 0.0f;
    float top    = 0.0f;
    float bottom = 0.0f;
};

// Corner order: [0]=TL [1]=TR [2]=BR [3]=BL
class WarpSurface {
public:
    WarpSurface();
    ~WarpSurface();

    // Render warped texture into the given screen rectangle (pixels)
    void render(GLuint texture, int vpX, int vpY, int vpW, int vpH);

    // Reset to full viewport quad with a small margin
    void reset(float margin = 0.05f);

    std::array<WarpPt, 4> pts;
    ColorAdj              adj;
    EdgeBlend             blend;
    std::vector<Mask>     masks;

private:
    void initGL();
    void updateHomography();

    GLuint m_vao    = 0;
    GLuint m_vbo    = 0;
    GLuint m_shader = 0;
    float  m_H[9]  = {};  // homography: viewport UV → texture UV
    bool   m_glInit = false;
};
