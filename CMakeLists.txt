cmake_minimum_required(VERSION 3.20)
project(GravityEngine VERSION 1.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to an optimized build for single-config generators (Ninja, Makefiles).
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GRAVITY_BUILD_APP   "Build the graphical application (requires raylib)" ON)
option(GRAVITY_BUILD_TESTS "Build the headless physics tests" ON)

# Warnings are applied per target so they never leak into third-party code (raylib).
function(gravity_set_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Physics core: no graphics dependency, can be built and tested anywhere.
# ---------------------------------------------------------------------------
add_library(gravity_core STATIC
    src/GravityEngine.cpp
    src/Scenes.cpp
    src/PhysicsTests.cpp
    include/Vec2.hpp
    include/Rgba.hpp
    include/Body.hpp
    include/Effects.hpp
    include/GravityEngine.hpp
    include/LaunchSettings.hpp
    include/PhysicsTests.hpp
)
target_include_directories(gravity_core PUBLIC include)
gravity_set_warnings(gravity_core)

if (GRAVITY_BUILD_TESTS)
    enable_testing()
    add_executable(GravityTests tests/run_tests.cpp)
    target_link_libraries(GravityTests PRIVATE gravity_core)
    gravity_set_warnings(GravityTests)
    add_test(NAME physics COMMAND GravityTests)
endif()

# ---------------------------------------------------------------------------
# Graphical application
# ---------------------------------------------------------------------------
if (GRAVITY_BUILD_APP)
    find_package(raylib 5.0 QUIET)
    if (NOT raylib_FOUND)
        message(STATUS "raylib not found locally, fetching via FetchContent...")
        include(FetchContent)
        FetchContent_Declare(
            raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5
            GIT_SHALLOW TRUE
        )
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(raylib)
    else()
        message(STATUS "Found local raylib: ${raylib_DIR}")
    endif()

    add_executable(GravityEngine
        src/main.cpp
        src/Renderer.cpp
        src/Sidebar.cpp
        src/Ui.cpp
        include/Renderer.hpp
        include/Ui.hpp
    )
    target_link_libraries(GravityEngine PRIVATE gravity_core raylib)
    gravity_set_warnings(GravityEngine)

    if (WIN32 AND MINGW)
        target_link_libraries(GravityEngine PRIVATE opengl32 gdi32 winmm)
    endif()
endif()
