#pragma once
#include "WarpSurface.h"
#include "Mask.h"
#include <array>
#include <string>
#include <vector>

struct ProjectState {
    std::array<WarpPt, 4> warpPts;
    int  warpMode     = 0;   // 0 = Quad, 1 = Mesh
    int  meshRows     = 4;
    int  meshCols     = 4;
    std::vector<float> meshPts;  // flat (meshRows+1)*(meshCols+1)*2
    ColorAdj          colorAdj;
    EdgeBlend         blend;
    std::vector<Mask> masks;
    int  monitor      = 1;
    int  activeSource = 0;   // 0 = PipeWire/Syphon, 1 = NDI
    std::string ndiSource;
};

bool saveProject(const std::string& path, const ProjectState& s);
bool loadProject(const std::string& path, ProjectState& s);
