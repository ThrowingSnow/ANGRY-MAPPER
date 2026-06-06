#include "WarpSurface.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>

// ============================================================
// Homography solver (DLT, 4-point correspondences)
// Computes H : src[i] → dst[i] using Gaussian elimination.
// H is stored row-major: [h0 h1 h2 / h3 h4 h5 / h6 h7 1]
// ============================================================
static bool solveHomography(const float src[4][2], const float dst[4][2], float H[9]) {
    // Each correspondence gives 2 equations for 8 unknowns (h8=1 fixed):
    //   h0*sx + h1*sy + h2 - h6*sx*dx - h7*sy*dx = dx
    //   h3*sx + h4*sy + h5 - h6*sx*dy - h7*sy*dy = dy
    double A[8][9] = {};
    for (int i = 0; i < 4; i++) {
        double sx = src[i][0], sy = src[i][1];
        double dx = dst[i][0], dy = dst[i][1];
        int r = 2 * i;
        A[r  ][0]=sx; A[r  ][1]=sy; A[r  ][2]=1;
        A[r  ][6]=-sx*dx; A[r  ][7]=-sy*dx; A[r  ][8]=dx;
        A[r+1][3]=sx; A[r+1][4]=sy; A[r+1][5]=1;
        A[r+1][6]=-sx*dy; A[r+1][7]=-sy*dy; A[r+1][8]=dy;
    }

    // Gaussian elimination with partial pivoting
    for (int col = 0; col < 8; col++) {
        int piv = col;
        for (int row = col+1; row < 8; row++)
            if (std::fabs(A[row][col]) > std::fabs(A[piv][col])) piv = row;
        if (std::fabs(A[piv][col]) < 1e-10) return false;
        for (int k = 0; k < 9; k++) std::swap(A[col][k], A[piv][k]);
        for (int row = 0; row < 8; row++) {
            if (row == col) continue;
            double f = A[row][col] / A[col][col];
            for (int k = col; k < 9; k++) A[row][k] -= f * A[col][k];
        }
    }

    for (int i = 0; i < 8; i++) H[i] = (float)(A[i][8] / A[i][i]);
    H[8] = 1.0f;
    return true;
}

// ============================================================
// GLSL shaders
// ============================================================
static const char* kVert = R"(
#version 330 core
layout(location=0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// The fragment shader applies the inverse homography H.
// H maps viewport UV [0,1]² → texture UV [0,1]².
// Pixels outside the user quad map outside [0,1]² → drawn black.
static const char* kFrag = R"(
#version 330 core
in  vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform mat3      uH;
uniform float     uBrightness;
uniform float     uContrast;
uniform float     uGamma;
void main() {
    vec3 t  = uH * vec3(vUV, 1.0);
    vec2 uv = t.xy / t.z;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec4 s = texture(uTex, uv);
    vec3 c = s.rgb;
    c += uBrightness;
    c  = (c - 0.5) * uContrast + 0.5;
    c  = pow(max(c, vec3(0.0)), vec3(1.0 / uGamma));
    fragColor = vec4(clamp(c, 0.0, 1.0), s.a);
}
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "[shader] %s\n", log);
    }
    return s;
}

// ============================================================
// WarpSurface
// ============================================================

WarpSurface::WarpSurface() {
    reset(0.0f);
}

WarpSurface::~WarpSurface() {
    if (m_vao)    glDeleteVertexArrays(1, &m_vao);
    if (m_vbo)    glDeleteBuffers(1, &m_vbo);
    if (m_shader) glDeleteProgram(m_shader);
}

void WarpSurface::reset(float m) {
    pts[0] = {m,     m    };   // TL
    pts[1] = {1-m,   m    };   // TR
    pts[2] = {1-m,   1-m  };   // BR
    pts[3] = {m,     1-m  };   // BL
}

void WarpSurface::initGL() {
    // Full-screen quad (two triangles)
    float verts[] = { -1,-1,  1,-1,  1,1,  -1,-1,  1,1,  -1,1 };
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    GLuint vert = compileShader(GL_VERTEX_SHADER, kVert);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, kFrag);
    m_shader = glCreateProgram();
    glAttachShader(m_shader, vert);
    glAttachShader(m_shader, frag);
    glLinkProgram(m_shader);
    glDeleteShader(vert);
    glDeleteShader(frag);

    m_glInit = true;
}

void WarpSurface::updateHomography() {
    // src = control points (viewport UV [0,1]²)
    // dst = texture corners: TL=(0,0) TR=(1,0) BR=(1,1) BL=(0,1)
    float src[4][2] = {
        {pts[0].x, pts[0].y},
        {pts[1].x, pts[1].y},
        {pts[2].x, pts[2].y},
        {pts[3].x, pts[3].y},
    };
    float dst[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
    solveHomography(src, dst, m_H);
}

void WarpSurface::render(GLuint texture, int vpX, int vpY, int vpW, int vpH) {
    if (!m_glInit) initGL();
    if (!texture) return;

    updateHomography();

    glViewport(vpX, vpY, vpW, vpH);

    glUseProgram(m_shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(m_shader, "uTex"), 0);

    // Upload homography as mat3 (column-major for OpenGL)
    // m_H is row-major [h0 h1 h2 / h3 h4 h5 / h6 h7 h8]
    // column-major for glUniformMatrix3fv: transpose
    float col[9] = {
        m_H[0], m_H[3], m_H[6],
        m_H[1], m_H[4], m_H[7],
        m_H[2], m_H[5], m_H[8],
    };
    glUniformMatrix3fv(glGetUniformLocation(m_shader, "uH"), 1, GL_FALSE, col);
    glUniform1f(glGetUniformLocation(m_shader, "uBrightness"), adj.brightness);
    glUniform1f(glGetUniformLocation(m_shader, "uContrast"),   adj.contrast);
    glUniform1f(glGetUniformLocation(m_shader, "uGamma"),      adj.gamma);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glUseProgram(0);
}
