#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nfd.h>
#include <stdio.h>
#include <cmath>
#include <algorithm>
#include <memory>
#include "NDISource.h"
#include "PipeWireSource.h"
#include "WarpSurface.h"
#include "Project.h"

// ----------------------------------------------------------------
// Output window helpers
// ----------------------------------------------------------------
static GLFWwindow* openOutputWindow(int monitorIdx, GLFWwindow* sharedCtx) {
    int count;
    GLFWmonitor** mons = glfwGetMonitors(&count);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);

    GLFWwindow* win = nullptr;

    if (monitorIdx >= 0 && monitorIdx < count) {
        // Fullscreen-style: borderless window covering the monitor
        GLFWmonitor* mon = mons[monitorIdx];
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        win = glfwCreateWindow(mode->width, mode->height,
                               "ANGRY-MAPPER output", nullptr, sharedCtx);
        if (win) {
            int mx, my;
            glfwGetMonitorPos(mon, &mx, &my);
            glfwSetWindowPos(win, mx, my);
        }
    } else {
        // Windowed fallback (single-monitor testing)
        win = glfwCreateWindow(1280, 720,
                               "ANGRY-MAPPER output", nullptr, sharedCtx);
    }

    // Restore hints for any subsequent windows
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    return win;
}

// ----------------------------------------------------------------
int main() {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1400, 820, "ANGRY-MAPPER", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    glewInit();
    NFD_Init();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Sources
    auto ndi = std::make_unique<NDISource>();
    auto pw  = std::make_unique<PipeWireSource>();
    std::vector<std::string> ndiSources;
    std::string ndiConnected;

    enum class ActiveSrc { None, NDI, PipeWire };
    ActiveSrc active = ActiveSrc::PipeWire;

    // Warp — one for preview (main ctx), one for output (output ctx, own VAO)
    auto warp    = std::make_unique<WarpSurface>();
    auto warpOut = std::make_unique<WarpSurface>();

    int dragIdx = -1;

    static char saveMsg[64] = {};

    // Output window state
    GLFWwindow* outputWin       = nullptr;
    int         selectedMonitor = 1;  // default: second monitor

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Close output window if its X was hit
        if (outputWin && glfwWindowShouldClose(outputWin)) {
            glfwDestroyWindow(outputWin);
            outputWin = nullptr;
            glfwMakeContextCurrent(window);
        }

        ndi->update();
        pw->update();

        GLuint activeTex = 0;
        int    activeW = 0, activeH = 0;
        if (active == ActiveSrc::PipeWire && pw->isConnected()) {
            activeTex = pw->texture(); activeW = pw->width(); activeH = pw->height();
        } else if (active == ActiveSrc::NDI && ndi->isConnected()) {
            activeTex = ndi->texture(); activeW = ndi->width(); activeH = ndi->height();
        }

        // ---- Render output window (before ImGui frame) ----
        if (outputWin) {
            warpOut->pts = warp->pts;
            glfwMakeContextCurrent(outputWin);
            int ow, oh;
            glfwGetFramebufferSize(outputWin, &ow, &oh);
            glViewport(0, 0, ow, oh);
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
            warpOut->render(activeTex, 0, 0, ow, oh);
            glfwSwapBuffers(outputWin);
            glfwMakeContextCurrent(window);
        }

        // ---- ImGui frame ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ---- Sources panel ----
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({300, 500}, ImGuiCond_Once);
        ImGui::Begin("Sources");

        ImGui::SeparatorText("PipeWire (Linux)");
        ImGui::Text("State: %s", pw->stateStr());
        if (pw->state() == PipeWireSource::State::Idle ||
            pw->state() == PipeWireSource::State::Error) {
            if (ImGui::Button("Capture Window...")) pw->requestCapture();
        }
        if (pw->isConnected()) {
            ImGui::Text("%dx%d", pw->width(), pw->height());
            bool sel = (active == ActiveSrc::PipeWire);
            if (ImGui::Checkbox("Use##pw", &sel) && sel) active = ActiveSrc::PipeWire;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("NDI");
        if (ImGui::Button("Refresh##ndi")) ndiSources = ndi->listSources();
        ImGui::SameLine();
        ImGui::Text("%zu found", ndiSources.size());
        for (const auto& src : ndiSources) {
            bool sel = (src == ndiConnected);
            if (ImGui::Selectable(src.c_str(), sel)) {
                if (ndi->connect(src)) { ndiConnected = src; active = ActiveSrc::NDI; }
            }
        }

        // ---- Monitor / Output ----
        ImGui::Spacing();
        ImGui::SeparatorText("Output");

        int monCount;
        GLFWmonitor** mons = glfwGetMonitors(&monCount);

        // Build monitor name list
        const char* monNames[16] = {};
        int nMons = std::min(monCount, 16);
        for (int i = 0; i < nMons; i++) monNames[i] = glfwGetMonitorName(mons[i]);
        // Extra option: windowed (single-monitor test)
        static const char* kWindowed = "Windowed (test)";
        const char* allNames[17];
        for (int i = 0; i < nMons; i++) allNames[i] = monNames[i];
        allNames[nMons] = kWindowed;
        int totalOpts = nMons + 1;

        // Clamp selection
        if (selectedMonitor >= totalOpts) selectedMonitor = 0;
        ImGui::Combo("Monitor", &selectedMonitor, allNames, totalOpts);

        bool outOpen = outputWin != nullptr;
        if (!outOpen) {
            if (ImGui::Button("Open Output")) {
                int monIdx = (selectedMonitor < nMons) ? selectedMonitor : -1;
                outputWin = openOutputWindow(monIdx, window);
            }
        } else {
            if (ImGui::Button("Close Output")) {
                glfwDestroyWindow(outputWin);
                outputWin = nullptr;
                glfwMakeContextCurrent(window);
            }
            ImGui::SameLine();
            ImGui::TextColored({0.2f,1.f,0.2f,1.f}, "LIVE");
        }

        // ---- Project Save / Load ----
        ImGui::Spacing();
        ImGui::SeparatorText("Project");

        auto collectState = [&]() {
            ProjectState ps;
            ps.warpPts     = warp->pts;
            ps.monitor     = selectedMonitor;
            ps.activeSource = (active == ActiveSrc::NDI) ? 1 : 0;
            ps.ndiSource   = ndiConnected;
            return ps;
        };

        if (ImGui::Button("Save...")) {
            nfdchar_t* outPath = nullptr;
            nfdfilteritem_t filters[] = {{"ANGRY-MAPPER project", "angrymap"}};
            if (NFD_SaveDialog(&outPath, filters, 1, nullptr, "mapping.angrymap") == NFD_OKAY) {
                auto ps = collectState();
                snprintf(saveMsg, sizeof(saveMsg),
                         saveProject(outPath, ps) ? "Saved." : "Save failed!");
                NFD_FreePath(outPath);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load...")) {
            nfdchar_t* inPath = nullptr;
            nfdfilteritem_t filters[] = {{"ANGRY-MAPPER project", "angrymap"}};
            if (NFD_OpenDialog(&inPath, filters, 1, nullptr) == NFD_OKAY) {
                ProjectState ps;
                if (loadProject(inPath, ps)) {
                    warp->pts      = ps.warpPts;
                    selectedMonitor = ps.monitor;
                    active = (ps.activeSource == 1) ? ActiveSrc::NDI : ActiveSrc::PipeWire;
                    ndiConnected   = ps.ndiSource;
                    snprintf(saveMsg, sizeof(saveMsg), "Loaded.");
                } else {
                    snprintf(saveMsg, sizeof(saveMsg), "Load failed!");
                }
                NFD_FreePath(inPath);
            }
        }
        if (saveMsg[0]) { ImGui::SameLine(); ImGui::TextDisabled("%s", saveMsg); }

        ImGui::End();

        // ---- Warp preview ----
        ImGui::SetNextWindowPos({320, 10}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({1060, 790}, ImGuiCond_Once);
        ImGui::Begin("Warp Preview", nullptr,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 avail     = ImGui::GetContentRegionAvail();
        float  cW = avail.x, cH = avail.y - 30.f;

        // InvisibleButton owns the mouse in this area (prevents window drag)
        ImGui::InvisibleButton("canvas", {cW, cH}, ImGuiButtonFlags_MouseButtonLeft);
        bool canvasActive  = ImGui::IsItemActive();
        bool canvasHovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Texture background
        if (activeTex) {
            dl->AddImage((ImTextureID)(intptr_t)activeTex,
                         canvasPos, {canvasPos.x + cW, canvasPos.y + cH});
        } else {
            dl->AddRectFilled(canvasPos, {canvasPos.x+cW, canvasPos.y+cH},
                              IM_COL32(24,24,24,255));
            dl->AddText({canvasPos.x + cW*0.5f - 55, canvasPos.y + cH*0.5f},
                        IM_COL32(100,100,100,255), "No source active");
        }

        // Coordinate helper
        auto toScreen = [&](WarpPt p) -> ImVec2 {
            return { canvasPos.x + p.x * cW, canvasPos.y + p.y * cH };
        };

        // Quad outline
        for (int i = 0; i < 4; i++) {
            dl->AddLine(toScreen(warp->pts[i]),
                        toScreen(warp->pts[(i+1)%4]),
                        IM_COL32(255, 200, 0, 180), 1.5f);
        }

        // Handles + drag
        ImVec2 mp = ImGui::GetIO().MousePos;
        if (!ImGui::GetIO().MouseDown[0]) dragIdx = -1;

        for (int i = 0; i < 4; i++) {
            ImVec2 sp   = toScreen(warp->pts[i]);
            float  dist = sqrtf((mp.x-sp.x)*(mp.x-sp.x) + (mp.y-sp.y)*(mp.y-sp.y));
            bool   hov  = dist < 12.f && canvasHovered;

            if (hov && ImGui::GetIO().MouseDown[0] && dragIdx == -1 && canvasActive)
                dragIdx = i;

            if (dragIdx == i) {
                warp->pts[i].x = std::clamp((mp.x - canvasPos.x) / cW, 0.f, 1.f);
                warp->pts[i].y = std::clamp((mp.y - canvasPos.y) / cH, 0.f, 1.f);
            }

            ImU32 col = dragIdx == i  ? IM_COL32(255,100,  0,255)
                      : hov           ? IM_COL32(255,255,  0,255)
                                      : IM_COL32(255,200,  0,210);
            dl->AddCircleFilled(sp, 9.f, col);
            dl->AddCircle(sp, 9.f, IM_COL32(0,0,0,220), 0, 2.f);
        }

        if (ImGui::Button("Reset Warp")) warp->reset(0.f);
        ImGui::SameLine();
        ImGui::TextDisabled("Drag corners to warp | Output appears on selected monitor");

        ImGui::End();

        // ---- Render main window ----
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.08f, 0.08f, 0.08f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    if (outputWin) glfwDestroyWindow(outputWin);
    NFD_Quit();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
