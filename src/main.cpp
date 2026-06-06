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
#include <filesystem>
#include <stb_image.h>
#include "NDISource.h"
#include "WarpSurface.h"
#include "MeshWarp.h"
#include "Project.h"

#ifdef __APPLE__
  #ifdef HAVE_SYPHON
    #include "SyphonSource.h"
  #endif
#elif defined(__linux__)
  #include "PipeWireSource.h"
#endif

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
        win = glfwCreateWindow(1280, 720,
                               "ANGRY-MAPPER output", nullptr, sharedCtx);
    }

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

    // Window icon — load assets/icon.png if present
    {
        int iw, ih, ic;
        unsigned char* px = stbi_load(ASSETS_DIR "/icon.png", &iw, &ih, &ic, 4);
        if (px) {
            GLFWimage img{ iw, ih, px };
            glfwSetWindowIcon(window, 1, &img);
            stbi_image_free(px);
        }
    }

    NFD_Init();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ---- Sources ----
    auto ndi = std::make_unique<NDISource>();
    std::vector<std::string> ndiSources;
    std::string ndiConnected;

#ifdef __APPLE__
  #ifdef HAVE_SYPHON
    auto syphon = std::make_unique<SyphonSource>();
    std::vector<std::string> syphonSources;
    std::string syphonConnected;
  #endif
#elif defined(__linux__)
    auto pw = std::make_unique<PipeWireSource>();
#endif

    enum class ActiveSrc { None, NDI,
#ifdef __APPLE__
        Syphon,
#else
        PipeWire,
#endif
    };

#ifdef __APPLE__
    ActiveSrc active = ActiveSrc::
  #ifdef HAVE_SYPHON
        Syphon;
  #else
        None;
  #endif
#else
    ActiveSrc active = ActiveSrc::PipeWire;
#endif

    // ---- Warp surfaces (one per GL context — VAOs not shared) ----
    auto warp    = std::make_unique<WarpSurface>();
    auto warpOut = std::make_unique<WarpSurface>();
    auto mesh    = std::make_unique<MeshWarp>();
    auto meshOut = std::make_unique<MeshWarp>();

    int      warpMode    = 0;   // 0 = Quad, 1 = Mesh
    int      dragIdx     = -1;
    int      meshDragIdx = -1;
    bool     showOverlay = true;
    ColorAdj colorAdj;          // shared between quad + mesh

    static char saveMsg[64] = {};

    GLFWwindow* outputWin       = nullptr;
    int         selectedMonitor = 1;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // ---- Keyboard shortcuts ----
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            if (ImGui::IsKeyPressed(ImGuiKey_H))
                showOverlay = !showOverlay;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                if (warpMode == 0) warp->reset(0.f);
                else { mesh->reset(); meshDragIdx = -1; }
            }
        }

        if (outputWin && glfwWindowShouldClose(outputWin)) {
            glfwDestroyWindow(outputWin);
            outputWin = nullptr;
            glfwMakeContextCurrent(window);
        }

        // ---- Update sources ----
        ndi->update();
#ifdef __APPLE__
  #ifdef HAVE_SYPHON
        syphon->update();
  #endif
#elif defined(__linux__)
        pw->update();
#endif

        GLuint activeTex = 0;
        int    activeW = 0, activeH = 0;

#ifdef __APPLE__
  #ifdef HAVE_SYPHON
        if (active == ActiveSrc::Syphon && syphon->isConnected()) {
            activeTex = syphon->texture();
            activeW   = syphon->width();
            activeH   = syphon->height();
        }
  #endif
#elif defined(__linux__)
        if (active == ActiveSrc::PipeWire && pw->isConnected()) {
            activeTex = pw->texture(); activeW = pw->width(); activeH = pw->height();
        }
#endif
        if (active == ActiveSrc::NDI && ndi->isConnected()) {
            activeTex = ndi->texture(); activeW = ndi->width(); activeH = ndi->height();
        }

        // Sync color adj to all warp instances
        warp->adj = warpOut->adj = mesh->adj = meshOut->adj = colorAdj;

        // ---- Render output window ----
        if (outputWin) {
            glfwMakeContextCurrent(outputWin);
            int ow, oh;
            glfwGetFramebufferSize(outputWin, &ow, &oh);
            glViewport(0, 0, ow, oh);
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
            if (warpMode == 0) {
                warpOut->pts = warp->pts;
                warpOut->render(activeTex, 0, 0, ow, oh);
            } else {
                meshOut->rows = mesh->rows;
                meshOut->cols = mesh->cols;
                meshOut->pts  = mesh->pts;
                meshOut->render(activeTex, 0, 0, ow, oh);
            }
            glfwSwapBuffers(outputWin);
            glfwMakeContextCurrent(window);
        }

        // ---- ImGui frame ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ---- Sources panel ----
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({300, 580}, ImGuiCond_Once);
        ImGui::Begin("Sources");

#ifdef __APPLE__
        // ---- Syphon (macOS) ----
  #ifdef HAVE_SYPHON
        ImGui::SeparatorText("Syphon (macOS)");
        if (ImGui::Button("Refresh##syphon")) syphonSources = syphon->listSources();
        ImGui::SameLine();
        ImGui::Text("%zu found", syphonSources.size());
        for (const auto& src : syphonSources) {
            bool sel = (src == syphonConnected);
            if (ImGui::Selectable(src.c_str(), sel)) {
                if (syphon->connect(src)) { syphonConnected = src; active = ActiveSrc::Syphon; }
            }
        }
        if (syphon->isConnected()) {
            ImGui::Text("%dx%d", syphon->width(), syphon->height());
            bool sel = (active == ActiveSrc::Syphon);
            if (ImGui::Checkbox("Use##syphon", &sel) && sel) active = ActiveSrc::Syphon;
        }
  #else
        ImGui::TextDisabled("Syphon not built (framework missing)");
  #endif

#elif defined(__linux__)
        // ---- PipeWire (Linux) ----
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
#endif

        // ---- NDI (cross-platform) ----
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

        // ---- Color Correction ----
        ImGui::Spacing();
        ImGui::SeparatorText("Color");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Brightness##col", &colorAdj.brightness, -1.f, 1.f, "%.2f");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Contrast##col",   &colorAdj.contrast,    0.f, 4.f, "%.2f");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Gamma##col",      &colorAdj.gamma,       0.1f,4.f, "%.2f");
        if (ImGui::Button("Reset Color")) colorAdj = ColorAdj{};

        // ---- Monitor / Output ----
        ImGui::Spacing();
        ImGui::SeparatorText("Output");

        int monCount;
        GLFWmonitor** mons = glfwGetMonitors(&monCount);
        const char* monNames[16] = {};
        int nMons = std::min(monCount, 16);
        for (int i = 0; i < nMons; i++) monNames[i] = glfwGetMonitorName(mons[i]);
        static const char* kWindowed = "Windowed (test)";
        const char* allNames[17];
        for (int i = 0; i < nMons; i++) allNames[i] = monNames[i];
        allNames[nMons] = kWindowed;
        int totalOpts = nMons + 1;
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
            ps.warpPts      = warp->pts;
            ps.warpMode     = warpMode;
            ps.meshRows     = mesh->rows;
            ps.meshCols     = mesh->cols;
            ps.meshPts      = mesh->pts;
            ps.colorAdj     = colorAdj;
            ps.monitor      = selectedMonitor;
            ps.activeSource = (active == ActiveSrc::NDI) ? 1 : 0;
            ps.ndiSource    = ndiConnected;
            return ps;
        };

        // "Load Last" — restores the auto-save from previous session
        {
            namespace fs = std::filesystem;
            fs::path autosave = fs::path(getenv("HOME")) / ".local/share/angry-mapper/autosave.angrymap";
            if (fs::exists(autosave)) {
                if (ImGui::Button("Load Last")) {
                    ProjectState ps;
                    if (loadProject(autosave.string(), ps)) {
                        warp->pts       = ps.warpPts;
                        warpMode        = ps.warpMode;
                        mesh->setGrid(ps.meshRows, ps.meshCols);
                        if (!ps.meshPts.empty()) mesh->pts = ps.meshPts;
                        colorAdj        = ps.colorAdj;
                        selectedMonitor = ps.monitor;
                        active = (ps.activeSource == 1) ? ActiveSrc::NDI : active;
                        ndiConnected    = ps.ndiSource;
                        snprintf(saveMsg, sizeof(saveMsg), "Restored.");
                    }
                }
                ImGui::SameLine();
            }
        }

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
                    warp->pts       = ps.warpPts;
                    warpMode        = ps.warpMode;
                    mesh->setGrid(ps.meshRows, ps.meshCols);
                    if (!ps.meshPts.empty()) mesh->pts = ps.meshPts;
                    colorAdj        = ps.colorAdj;
                    selectedMonitor = ps.monitor;
                    active = (ps.activeSource == 1) ? ActiveSrc::NDI : active;
                    ndiConnected    = ps.ndiSource;
                    snprintf(saveMsg, sizeof(saveMsg), "Loaded.");
                } else {
                    snprintf(saveMsg, sizeof(saveMsg), "Load failed!");
                }
                NFD_FreePath(inPath);
            }
        }
        if (saveMsg[0]) { ImGui::SameLine(); ImGui::TextDisabled("%s", saveMsg); }

        ImGui::End();

        // ---- Warp Preview ----
        static bool previewLocked = true;
        ImGui::SetNextWindowPos({320, 10}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({1060, 790}, ImGuiCond_Once);
        ImGuiWindowFlags previewFlags = ImGuiWindowFlags_NoScrollbar
                                      | ImGuiWindowFlags_NoScrollWithMouse;
        if (previewLocked) previewFlags |= ImGuiWindowFlags_NoMove;
        ImGui::Begin("Warp Preview", nullptr, previewFlags);

        // Mode radio + grid controls + window lock
        ImGui::RadioButton("Quad", &warpMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Mesh", &warpMode, 1);
        if (warpMode == 1) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            int newRows = mesh->rows;
            if (ImGui::SliderInt("##rows", &newRows, 2, 16)) {
                mesh->setGrid(newRows, mesh->cols);
                meshDragIdx = -1;
            }
            ImGui::SameLine(); ImGui::TextDisabled("rows");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            int newCols = mesh->cols;
            if (ImGui::SliderInt("##cols", &newCols, 2, 16)) {
                mesh->setGrid(mesh->rows, newCols);
                meshDragIdx = -1;
            }
            ImGui::SameLine(); ImGui::TextDisabled("cols");
        }
        ImGui::SameLine(0, 20);
        ImGui::Checkbox(showOverlay ? "Overlay [on]" : "Overlay [off]", &showOverlay);
        ImGui::SameLine(0, 12);
        ImGui::Checkbox(previewLocked ? "Lock [on]" : "Lock [off]", &previewLocked);

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 avail     = ImGui::GetContentRegionAvail();
        float  cW = avail.x, cH = avail.y - 30.f;

        ImGui::InvisibleButton("canvas", {cW, cH}, ImGuiButtonFlags_MouseButtonLeft);
        bool canvasActive  = ImGui::IsItemActive();
        bool canvasHovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();

        if (activeTex) {
            dl->AddImage((ImTextureID)(intptr_t)activeTex,
                         canvasPos, {canvasPos.x + cW, canvasPos.y + cH});
        } else {
            dl->AddRectFilled(canvasPos, {canvasPos.x+cW, canvasPos.y+cH},
                              IM_COL32(24,24,24,255));
            dl->AddText({canvasPos.x + cW*0.5f - 55, canvasPos.y + cH*0.5f},
                        IM_COL32(100,100,100,255), "No source active");
        }

        ImVec2 mp = ImGui::GetIO().MousePos;
        auto toScreen = [&](WarpPt p) -> ImVec2 {
            return { canvasPos.x + p.x * cW, canvasPos.y + p.y * cH };
        };

        if (warpMode == 0) {
            // ---- Quad mode ----
            if (showOverlay) {
                for (int i = 0; i < 4; i++)
                    dl->AddLine(toScreen(warp->pts[i]),
                                toScreen(warp->pts[(i+1)%4]),
                                IM_COL32(255,200,0,180), 1.5f);
            }

            if (!ImGui::GetIO().MouseDown[0]) dragIdx = -1;
            for (int i = 0; i < 4; i++) {
                ImVec2 sp   = toScreen(warp->pts[i]);
                float  dist = sqrtf((mp.x-sp.x)*(mp.x-sp.x)+(mp.y-sp.y)*(mp.y-sp.y));
                bool   hov  = dist < 12.f && canvasHovered;
                if (hov && ImGui::GetIO().MouseDown[0] && dragIdx == -1 && canvasActive)
                    dragIdx = i;
                if (dragIdx == i) {
                    warp->pts[i].x = std::clamp((mp.x-canvasPos.x)/cW, 0.f, 1.f);
                    warp->pts[i].y = std::clamp((mp.y-canvasPos.y)/cH, 0.f, 1.f);
                }
                if (showOverlay) {
                    ImU32 col = dragIdx==i ? IM_COL32(255,100,0,255)
                              : hov        ? IM_COL32(255,255,0,255)
                                           : IM_COL32(255,200,0,210);
                    dl->AddCircleFilled(sp, 9.f, col);
                    dl->AddCircle(sp, 9.f, IM_COL32(0,0,0,220), 0, 2.f);
                }
            }

            if (ImGui::Button("Reset Warp")) warp->reset(0.f);
            ImGui::SameLine();
            ImGui::TextDisabled("H = overlay  R = reset  | Output on selected monitor");

        } else {
            // ---- Mesh mode ----
            int R = mesh->rows, C = mesh->cols;

            if (showOverlay) {
                for (int r = 0; r <= R; r++)
                    for (int c = 0; c < C; c++)
                        dl->AddLine(toScreen(mesh->getPt(r,c)), toScreen(mesh->getPt(r,c+1)),
                                    IM_COL32(255,200,0,160), 1.f);
                for (int c = 0; c <= C; c++)
                    for (int r = 0; r < R; r++)
                        dl->AddLine(toScreen(mesh->getPt(r,c)), toScreen(mesh->getPt(r+1,c)),
                                    IM_COL32(255,200,0,160), 1.f);
            }

            if (!ImGui::GetIO().MouseDown[0]) meshDragIdx = -1;
            for (int r = 0; r <= R; r++) {
                for (int c = 0; c <= C; c++) {
                    int    idx  = r*(C+1)+c;
                    WarpPt pt   = mesh->getPt(r,c);
                    ImVec2 sp   = toScreen(pt);
                    float  dist = sqrtf((mp.x-sp.x)*(mp.x-sp.x)+(mp.y-sp.y)*(mp.y-sp.y));
                    bool   hov  = dist < 9.f && canvasHovered;
                    if (hov && ImGui::GetIO().MouseDown[0] && meshDragIdx==-1 && canvasActive)
                        meshDragIdx = idx;
                    if (meshDragIdx == idx)
                        mesh->setPt(r, c, {
                            std::clamp((mp.x-canvasPos.x)/cW, 0.f, 1.f),
                            std::clamp((mp.y-canvasPos.y)/cH, 0.f, 1.f)
                        });
                    if (showOverlay) {
                        ImU32 col = meshDragIdx==idx ? IM_COL32(255,100,0,255)
                                  : hov              ? IM_COL32(255,255,0,255)
                                                     : IM_COL32(255,200,0,200);
                        dl->AddCircleFilled(sp, 6.f, col);
                        dl->AddCircle(sp, 6.f, IM_COL32(0,0,0,200), 0, 1.5f);
                    }
                }
            }

            if (ImGui::Button("Reset Mesh")) { mesh->reset(); meshDragIdx = -1; }
            ImGui::SameLine();
            ImGui::TextDisabled("H = overlay  R = reset  | Output on selected monitor");
        }

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

    // ---- Auto-save on exit ----
    {
        ProjectState ps;
        ps.warpPts      = warp->pts;
        ps.warpMode     = warpMode;
        ps.meshRows     = mesh->rows;
        ps.meshCols     = mesh->cols;
        ps.meshPts      = mesh->pts;
        ps.monitor      = selectedMonitor;
        ps.activeSource = (active == ActiveSrc::NDI) ? 1 : 0;
        ps.ndiSource    = ndiConnected;

        namespace fs = std::filesystem;
        fs::path p = fs::path(getenv("HOME")) / ".local/share/angry-mapper/autosave.angrymap";
        fs::create_directories(p.parent_path());
        saveProject(p.string(), ps);
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
