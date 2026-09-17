cmake_minimum_required(VERSION 3.20)
project(RezzOrder VERSION 0.4.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(REZZ_WARNINGS /W4 /permissive- /utf-8)

# ImGui must match the version Nexus renders with (Raidcore fork, 1.80).
add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC external/imgui)

# Game-independent logic: event definitions, revive skill table, tracker, live session.
add_library(rezz_core STATIC
    src/Arc.cpp
    src/Demo.cpp
    src/FakeSquad.cpp
    src/ReviveSkills.cpp
    src/Session.cpp
    src/Share.cpp
    src/Tracker.cpp
)
target_include_directories(rezz_core PUBLIC src)
target_compile_options(rezz_core PRIVATE ${REZZ_WARNINGS})

add_library(RezzOrder SHARED
    src/entry.cpp
    src/ArcStyle.cpp
    src/Banner.cpp
    src/Capture.cpp
    src/Diagnostics.cpp
    src/Fonts.cpp
    src/Icons.cpp
    src/Live.cpp
    src/Notify.cpp
    src/OrderUi.cpp
    src/Recorder.cpp
    src/Settings.cpp
    src/Ui.cpp
)
target_include_directories(RezzOrder PRIVATE src external)
target_compile_definitions(RezzOrder PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    REZZ_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    REZZ_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    REZZ_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
target_compile_options(RezzOrder PRIVATE ${REZZ_WARNINGS})
target_link_libraries(RezzOrder PRIVATE rezz_core imgui winmm)

# Everything below builds the development tools: the offline replay, the unit tests and the offscreen UI
# renderer. They need tools/, which a release checkout may not carry, so they are optional and skipped when
# the sources are not there.
option(REZZ_BUILD_TOOLS "Build the replay, tests and UI render harness" ON)
if(REZZ_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/replay.cpp")

# Offline replay of recorder CSVs through the capture code and the tracker (no game needed).
add_executable(rezz_replay
    tools/replay.cpp
    src/Capture.cpp
    src/Recorder.cpp
)
target_include_directories(rezz_replay PRIVATE src external)
target_compile_definitions(rezz_replay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(rezz_replay PRIVATE ${REZZ_WARNINGS})
target_link_libraries(rezz_replay PRIVATE rezz_core winmm)

# Renders the addon's own UI offscreen (D3D11 + WARP) and saves a PNG per scenario: layout work without
# starting the game. Host.cpp replaces the pieces that only exist inside Nexus.
add_executable(rezz_uishot
    tools/uishot/main.cpp
    tools/uishot/Host.cpp
    tools/uishot/Scenarios.cpp
    src/Banner.cpp
    src/Notify.cpp
    src/OrderUi.cpp
    src/Settings.cpp
    external/imgui/backends/imgui_impl_dx11.cpp
)
target_include_directories(rezz_uishot PRIVATE src external tools/uishot)
target_compile_definitions(rezz_uishot PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(rezz_uishot PRIVATE ${REZZ_WARNINGS})
target_link_libraries(rezz_uishot PRIVATE rezz_core imgui d3d11 dxgi windowscodecs winmm)

add_executable(rezz_tracker_tests tools/tracker_tests.cpp src/Settings.cpp)
target_compile_options(rezz_tracker_tests PRIVATE ${REZZ_WARNINGS})
target_link_libraries(rezz_tracker_tests PRIVATE rezz_core)

endif()
