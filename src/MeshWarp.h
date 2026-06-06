#pragma once
#include <GL/glew.h>
#include <vector>
#include "WarpSurface.h"  // WarpPt

// NxM grid warp. pts is a flat (rows+1)*(cols+1)*2 array of [0,1] screen-space positions.
class MeshWarp {
public:
    int rows = 4, cols = 4;
    std::vector<float> pts;  // row-major, 2 floats per point

    MeshWarp() { reset(); }
    ~MeshWarp();

    void setGrid(int r, int c);  // change resolution and reset
    void reset();                 // reset current grid to uniform
    void render(GLuint texture, int vpX, int vpY, int vpW, int vpH);

    WarpPt getPt(int r, int c) const;
    void   setPt(int r, int c, WarpPt p);

private:
    void   initGL();
    GLuint m_vao = 0, m_vbo = 0, m_shader = 0;
    bool   m_glInit = false;
};
