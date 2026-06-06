#include "MeshWarp.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

MeshWarp::~MeshWarp() {
    if (m_glInit) {
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_vbo);
        glDeleteProgram(m_shader);
    }
}

void MeshWarp::setGrid(int r, int c) {
    rows = r; cols = c;
    reset();
}

void MeshWarp::reset() {
    pts.resize((rows + 1) * (cols + 1) * 2);
    for (int r = 0; r <= rows; r++)
        for (int c = 0; c <= cols; c++)
            setPt(r, c, {(float)c / cols, (float)r / rows});
}

WarpPt MeshWarp::getPt(int r, int c) const {
    int i = (r * (cols + 1) + c) * 2;
    return {pts[i], pts[i + 1]};
}

void MeshWarp::setPt(int r, int c, WarpPt p) {
    int i = (r * (cols + 1) + c) * 2;
    pts[i] = p.x; pts[i + 1] = p.y;
}

static GLuint compileStage(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(s, 512, nullptr, buf);
        fprintf(stderr, "MeshWarp shader error: %s\n", buf);
    }
    return s;
}

void MeshWarp::initGL() {
    const char* vs = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
out vec2 vScreen;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUV    = aUV;
    vScreen = aPos * 0.5 + 0.5;
}
)";
    const char* fs = R"(
#version 330 core
uniform sampler2D uTex;
uniform float     uBrightness;
uniform float     uContrast;
uniform float     uGamma;
uniform float     uBlendL, uBlendR, uBlendT, uBlendB;
#define MAX_MASKS 8
uniform int   uMaskCount;
uniform int   uMaskIsCircle[MAX_MASKS];
uniform vec4  uMaskGeom[MAX_MASKS];
uniform float uMaskFeather[MAX_MASKS];
in  vec2 vUV;
in  vec2 vScreen;
out vec4 fragColor;
void main() {
    vec4 s = texture(uTex, vUV);
    vec3 c = s.rgb;
    c += uBrightness;
    c  = (c - 0.5) * uContrast + 0.5;
    c  = pow(max(c, vec3(0.0)), vec3(1.0 / uGamma));
    // vUV.y is 1 at top (r=0) and 0 at bottom (r=rows), so T/B are swapped vs. WarpSurface
    float bx = smoothstep(0.0, max(uBlendL, 0.001), vUV.x)
             * smoothstep(0.0, max(uBlendR, 0.001), 1.0 - vUV.x);
    float by = smoothstep(0.0, max(uBlendT, 0.001), 1.0 - vUV.y)
             * smoothstep(0.0, max(uBlendB, 0.001), vUV.y);
    c *= bx * by;
    if (uMaskCount > 0) {
        float acc = 0.0;
        for (int i = 0; i < MAX_MASKS; i++) {
            if (i >= uMaskCount) break;
            vec2  ctr = uMaskGeom[i].xy;
            float f   = max(uMaskFeather[i], 0.001);
            float mi;
            if (uMaskIsCircle[i] != 0) {
                float r    = uMaskGeom[i].z;
                float dist = length(vScreen - ctr);
                mi = 1.0 - smoothstep(r - f, r, dist);
            } else {
                float rx = uMaskGeom[i].z, ry = uMaskGeom[i].w;
                vec2  d  = abs(vScreen - ctr) - vec2(rx, ry);
                float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
                mi = 1.0 - smoothstep(-f, 0.0, dist);
            }
            acc = max(acc, mi);
        }
        c *= acc;
    }
    fragColor = vec4(clamp(c, 0.0, 1.0), s.a);
}
)";
    GLuint v = compileStage(GL_VERTEX_SHADER,   vs);
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs);
    m_shader = glCreateProgram();
    glAttachShader(m_shader, v); glAttachShader(m_shader, f);
    glLinkProgram(m_shader);
    glDeleteShader(v); glDeleteShader(f);

    glUseProgram(m_shader);
    glUniform1i(glGetUniformLocation(m_shader, "uTex"), 0);
    // color uniforms set to identity defaults at init
    glUniform1f(glGetUniformLocation(m_shader, "uBrightness"), 0.f);
    glUniform1f(glGetUniformLocation(m_shader, "uContrast"),   1.f);
    glUniform1f(glGetUniformLocation(m_shader, "uGamma"),      1.f);

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // stride = 4 floats: pos.xy, uv.xy
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    m_glInit = true;
}

void MeshWarp::render(GLuint texture, int vpX, int vpY, int vpW, int vpH) {
    if (!m_glInit) initGL();
    if (!texture) return;

    // Build vertex buffer: 2 triangles per cell, 6 verts × 4 floats
    int numVerts = rows * cols * 6;
    std::vector<float> verts;
    verts.reserve(numVerts * 4);

    // Screen [0,1] → NDC [-1,1]. Y is flipped (screen Y↓, GL Y↑).
    // Texture UV: u = col/cols, v = 1 - row/rows  (GL origin = bottom-left)
    auto emit = [&](int r, int c) {
        auto p  = getPt(r, c);
        float nx = p.x * 2.f - 1.f;
        float ny = 1.f - p.y * 2.f;
        float u  = (float)c / cols;
        float v  = 1.f - (float)r / rows;
        verts.push_back(nx); verts.push_back(ny);
        verts.push_back(u);  verts.push_back(v);
    };

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            emit(r,   c  );  // TL
            emit(r,   c+1);  // TR
            emit(r+1, c  );  // BL
            emit(r,   c+1);  // TR
            emit(r+1, c+1);  // BR
            emit(r+1, c  );  // BL
        }
    }

    glViewport(vpX, vpY, vpW, vpH);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUseProgram(m_shader);
    glUniform1f(glGetUniformLocation(m_shader, "uBrightness"), adj.brightness);
    glUniform1f(glGetUniformLocation(m_shader, "uContrast"),   adj.contrast);
    glUniform1f(glGetUniformLocation(m_shader, "uGamma"),      adj.gamma);
    glUniform1f(glGetUniformLocation(m_shader, "uBlendL"),     blend.left);
    glUniform1f(glGetUniformLocation(m_shader, "uBlendR"),     blend.right);
    glUniform1f(glGetUniformLocation(m_shader, "uBlendT"),     blend.top);
    glUniform1f(glGetUniformLocation(m_shader, "uBlendB"),     blend.bottom);

    int mc = std::min((int)masks.size(), 8);
    glUniform1i(glGetUniformLocation(m_shader, "uMaskCount"), mc);
    if (mc > 0) {
        int   types[8] = {};
        float geom[32] = {};
        float feat[8]  = {};
        for (int i = 0; i < mc; i++) {
            types[i]    = (masks[i].shape == MaskShape::Circle) ? 1 : 0;
            geom[i*4+0] = masks[i].cx;
            geom[i*4+1] = masks[i].cy;
            geom[i*4+2] = masks[i].rx;
            geom[i*4+3] = masks[i].ry;
            feat[i]     = masks[i].feather;
        }
        glUniform1iv(glGetUniformLocation(m_shader, "uMaskIsCircle"), 8, types);
        glUniform4fv(glGetUniformLocation(m_shader, "uMaskGeom"),     8, geom);
        glUniform1fv(glGetUniformLocation(m_shader, "uMaskFeather"),  8, feat);
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(verts.size() * sizeof(float)),
                 verts.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, numVerts);
    glBindVertexArray(0);
}
