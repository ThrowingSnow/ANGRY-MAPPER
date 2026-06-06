#include "Project.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

bool saveProject(const std::string& path, const ProjectState& s) {
    try {
        json j;
        j["version"] = 1;
        j["monitor"]      = s.monitor;
        j["activeSource"] = s.activeSource;
        j["ndiSource"]    = s.ndiSource;

        auto& pts = j["warp"]["pts"];
        for (const auto& p : s.warpPts)
            pts.push_back({{"x", p.x}, {"y", p.y}});

        std::ofstream f(path);
        f << j.dump(2);
        return f.good();
    } catch (...) {
        return false;
    }
}

bool loadProject(const std::string& path, ProjectState& s) {
    try {
        std::ifstream f(path);
        if (!f) return false;

        json j = json::parse(f);

        s.monitor      = j.value("monitor",      s.monitor);
        s.activeSource = j.value("activeSource", s.activeSource);
        s.ndiSource    = j.value("ndiSource",    std::string{});

        const auto& pts = j["warp"]["pts"];
        for (size_t i = 0; i < 4 && i < pts.size(); i++) {
            s.warpPts[i].x = pts[i]["x"];
            s.warpPts[i].y = pts[i]["y"];
        }
        return true;
    } catch (...) {
        return false;
    }
}
