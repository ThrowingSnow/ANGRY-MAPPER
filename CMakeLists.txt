cmake_minimum_required(VERSION 3.21)
project(ANGRY-MAPPER VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---

include(FetchContent)

# Dear ImGui (no CMake, compiled directly)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9
)
FetchContent_MakeAvailable(imgui)

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# nativefiledialog-extended
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG        v1.2.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nfd)

find_package(OpenGL REQUIRED)
find_package(GLEW   REQUIRED)
find_package(glfw3 3.3 REQUIRED)

# NDI SDK — cross-platform (Linux + macOS + Windows)
find_library(NDI_LIB ndi REQUIRED)
find_path(NDI_INCLUDE_DIR Processing.NDI.Lib.h REQUIRED)

# Platform-specific capture source
if(APPLE)
    # Syphon framework (install from https://github.com/Syphon/Syphon-Framework)
    find_library(SYPHON_FRAMEWORK Syphon
        PATHS /Library/Frameworks ~/Library/Frameworks
    )
    if(SYPHON_FRAMEWORK)
        message(STATUS "Syphon found: ${SYPHON_FRAMEWORK}")
        set(HAVE_SYPHON ON)
    else()
        message(WARNING "Syphon.framework not found in /Library/Frameworks or ~/Library/Frameworks — Syphon capture disabled")
        set(HAVE_SYPHON OFF)
    endif()
elseif(UNIX)
    # PipeWire + XDG portal (Linux)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
    pkg_check_modules(PORTAL   REQUIRED libportal)
endif()

# --- ImGui as compiled library ---
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

# --- Main executable ---
add_executable(angry-mapper
    src/main.cpp
    src/NDISource.cpp
    src/WarpSurface.cpp
    src/MeshWarp.cpp
    src/Project.cpp
)

target_include_directories(angry-mapper PRIVATE
    src/
    ${NDI_INCLUDE_DIR}
)

target_link_libraries(angry-mapper PRIVATE
    imgui_lib
    glfw
    OpenGL::GL
    GLEW::GLEW
    ${NDI_LIB}
    nlohmann_json::nlohmann_json
    nfd
)

# Platform-specific sources and link libs
if(APPLE)
    if(HAVE_SYPHON)
        target_sources(angry-mapper PRIVATE src/SyphonSource.mm)
        target_compile_definitions(angry-mapper PRIVATE HAVE_SYPHON=1)
        target_link_libraries(angry-mapper PRIVATE ${SYPHON_FRAMEWORK})
        set_source_files_properties(src/SyphonSource.mm PROPERTIES
            COMPILE_FLAGS "-fobjc-arc"
        )
    endif()
    # macOS system frameworks
    target_link_libraries(angry-mapper PRIVATE
        "-framework Cocoa"
        "-framework OpenGL"
    )
elseif(UNIX)
    target_sources(angry-mapper PRIVATE src/PipeWireSource.cpp)
    target_include_directories(angry-mapper PRIVATE
        ${PIPEWIRE_INCLUDE_DIRS}
        ${PORTAL_INCLUDE_DIRS}
    )
    target_compile_options(angry-mapper PRIVATE
        ${PIPEWIRE_CFLAGS_OTHER}
        ${PORTAL_CFLAGS_OTHER}
    )
    target_link_libraries(angry-mapper PRIVATE
        ${PIPEWIRE_LIBRARIES}
        ${PORTAL_LIBRARIES}
    )
endif()
