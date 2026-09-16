cmake_minimum_required(VERSION 3.20)
project(RezzOrder VERSION 0.1.0 LANGUAGES CXX)

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
    src/ReviveSkills.cpp
    src/Session.cpp
    src/Tracker.cpp
)
target_include_directories(rezz_core PUBLIC src)
target_compile_options(rezz_core PRIVATE ${REZZ_WARNINGS})

add_library(RezzOrder SHARED
    src/entry.cpp
    src/ArcStyle.cpp
    src/Capture.cpp
    src/Diagnostics.cpp
    src/Fonts.cpp
    src/Icons.cpp
    src/Live.cpp
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

add_executable(rezz_tracker_tests tools/tracker_tests.cpp)
target_compile_options(rezz_tracker_tests PRIVATE ${REZZ_WARNINGS})
target_link_libraries(rezz_tracker_tests PRIVATE rezz_core)
