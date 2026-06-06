# ANGRY-MAPPER — Roadmap

Cross-platform projector/display mapping tool.
Input: KodeLife (Syphon on macOS, NDI on Linux) → Warp → Projector/Display output.

---

## Phase 1 — Project Skeleton
- [x] CMake setup (GLFW + OpenGL + Dear ImGui via FetchContent)
- [x] Main window with OpenGL context
- [x] Basic ImGui overlay
- [x] Multi-display enumeration (list connected displays)
- [x] Fullscreen output on selected display (borderless window positioned at monitor origin)

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
- [x] **Syphon Source** (macOS)
  - [x] Obj-C++ bridge (.mm file)
  - [x] List available Syphon servers
  - [x] GL_TEXTURE_RECTANGLE → GL_TEXTURE_2D blit (SDK 5 always RECT)
- [x] CMake platform switch (`if(APPLE)` → Syphon, else → PipeWire)

## Phase 3 — Quad Warp Engine
- [x] Full-screen quad renderer (input texture → output)
- [x] 4-corner control points (drag in UI)
- [x] Perspective/homography transform (DLT solver + GLSL shader)
- [x] Visual overlay: control point handles + quad outline
- [x] Dual-window output (control window + borderless projector window)
- [x] Monitor selection (any connected display)
- [x] Toggle overlay on/off (H key + checkbox in UI)

## Phase 4 — Mesh Warp (optional, after Phase 3)
- [x] NxM grid of control points
- [x] Bilinear interpolation per triangle (linear mesh tessellation)
- [x] Grid resolution configurable (2–16 rows/cols via sliders)

## Phase 5 — Multi-Surface / Multi-Output
- [ ] Multiple warp surfaces per session
- [ ] Each surface: own input source + warp + output display
- [ ] Surface layering / z-order

## Phase 6 — Presets & Save/Load
- [x] JSON serialization of warp points + source config (nlohmann/json)
- [x] Save / Load project file (.angrymap) with native file dialog (nfd)
- [x] Auto-save on exit (~/.local/share/angry-mapper/autosave.angrymap)

## Phase 7 — Polish
- [x] Keyboard shortcuts: H = toggle overlay, R = reset warp/mesh, F = fullscreen output
- [x] GLSL edge blend shader per surface (L/R/T/B softness, smoothstep, saved in project)
- [x] Brightness / contrast / gamma per surface (ColorAdj sliders)
- [x] FPS counter display in Sources panel

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

**→ Phases 1–4 + 6 + 7 complete. Only remaining: Phase 5 (Multi-Surface / Multi-Output).**
