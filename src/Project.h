#pragma once
#include "WarpSurface.h"
#include <array>
#include <string>

struct ProjectState {
    std::array<WarpPt, 4> warpPts;
    int  monitor      = 1;
    int  activeSource = 0;  // 0 = PipeWire, 1 = NDI
    std::string ndiSource;
};

bool saveProject(const std::string& path, const ProjectState& s);
bool loadProject(const std::string& path, ProjectState& s);
