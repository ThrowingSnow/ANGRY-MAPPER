# ANGRY-MAPPER — Roadmap

Cross-platform projector/display mapping tool.
Input: KodeLife (Syphon on macOS, NDI on Linux) → Warp → Projector/Display output.

---

## Phase 1 — Project Skeleton
- [x] CMake setup (GLFW + OpenGL + Dear ImGui via FetchContent)
- [x] Main window with OpenGL context
- [x] Basic ImGui overlay
- [ ] Multi-display enumeration (list connected displays)
- [ ] Fullscreen output on selected display

## Phase 2 — Texture Input
- [x] Abstract `TextureSource` interface
- [x] **NDI Source** (Linux primary, cross-platform fallback)
  - [x] NDI SDK integration via CMake
  - [x] List available NDI sources
  - [x] Receive frames → upload to GL texture
  - [x] Live preview of incoming texture in ImGui window
- [x] **PipeWire Source** (Linux — replaces NDI for KodeLife capture)
  - [x] XDG ScreenCast portal via libportal (KDE picker dialog)
  - [x] PipeWire stream → GL texture
  - [x] Live preview confirmed working with KodeLife
- [ ] **Syphon Source** (macOS — future)
  - [ ] Obj-C++ bridge (.mm file)
  - [ ] List available Syphon servers
  - [ ] Zero-copy IOSurface → GL texture
- [ ] CMake platform switch (`if(APPLE)` → Syphon, else → PipeWire)

## Phase 3 — Quad Warp Engine
- [x] Full-screen quad renderer (input texture → output)
- [x] 4-corner control points (drag in UI)
- [x] Perspective/homography transform (DLT solver + GLSL shader)
- [x] Visual overlay: control point handles + quad outline
- [x] Dual-window output (control window + borderless projector window)
- [x] Monitor selection (any connected display)
- [ ] Toggle overlay on/off (hide for clean output)

## Phase 4 — Mesh Warp (optional, after Phase 3)
- [ ] NxM grid of control points
- [ ] Bilinear / bicubic interpolation between points
- [ ] Grid resolution configurable (e.g. 4x4 up to 16x16)

## Phase 5 — Multi-Surface / Multi-Output
- [ ] Multiple warp surfaces per session
- [ ] Each surface: own input source + warp + output display
- [ ] Surface layering / z-order

## Phase 6 — Presets & Save/Load
- [x] JSON serialization of warp points + source config (nlohmann/json)
- [x] Save / Load project file (.angrymap) with native KDE file dialog (nfd)
- [ ] Auto-save on exit

## Phase 7 — Polish
- [ ] Keyboard shortcuts (reset warp, toggle overlay, fullscreen)
- [ ] GLSL blend/mask shader per surface (edge blending)
- [ ] Brightness / contrast / gamma per surface
- [ ] Performance: target 60fps at 1080p+

---

## Tech Stack

| Layer | Library |
|---|---|
| Window / Input | GLFW |
| Rendering | OpenGL 3.3+ |
| UI | Dear ImGui |
| Build | CMake + Ninja |
| Texture Input (macOS) | Syphon Framework (Obj-C++) |
| Texture Input (Linux) | NDI SDK 6 |
| Serialization | nlohmann/json |

---

## Current Status

**→ Phase 3 done. Next: Phase 6 (Save/Load) or Phase 7 (Polish)**
