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
void main() { gl_Position = vec4(aPos, 0.0, 1.0); vUV = aUV; }
)";
    const char* fs = R"(
#version 330 core
uniform sampler2D uTex;
in  vec2 vUV;
out vec4 fragColor;
void main() { fragColor = texture(uTex, vUV); }
)";
    GLuint v = compileStage(GL_VERTEX_SHADER,   vs);
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs);
    m_shader = glCreateProgram();
    glAttachShader(m_shader, v); glAttachShader(m_shader, f);
    glLinkProgram(m_shader);
    glDeleteShader(v); glDeleteShader(f);

    glUseProgram(m_shader);
    glUniform1i(glGetUniformLocation(m_shader, "uTex"), 0);

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
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(verts.size() * sizeof(float)),
                 verts.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, numVerts);
    glBindVertexArray(0);
}
