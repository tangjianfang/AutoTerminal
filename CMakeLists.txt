cmake_minimum_required(VERSION 3.20)
project(AutoTerminal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
        _CRT_SECURE_NO_WARNINGS)
endif()

# ---- third-party ----------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

option(AUTOTERMINAL_BUILD_TESTS "Build unit tests" ON)
if(AUTOTERMINAL_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# ---- core library ---------------------------------------------------------
add_library(autoterminal_core STATIC
    src/logger.cpp
    src/tile_engine.cpp
    src/monitor_index.cpp
    src/config_store.cpp
    src/window_manager.cpp
    src/event_source.cpp
    src/ui_bridge.cpp
)
target_include_directories(autoterminal_core PUBLIC src)
target_link_libraries(autoterminal_core PUBLIC tomlplusplus::tomlplusplus)
if(WIN32)
    target_link_libraries(autoterminal_core PUBLIC
        User32 Gdi32 Shell32 Advapi32 SetupApi
        Ole32 Propsys Dwmapi)
endif()

# ---- main executable ------------------------------------------------------
add_executable(AutoTerminal WIN32 src/main.cpp)
target_link_libraries(AutoTerminal PRIVATE autoterminal_core)

# ---- tests ----------------------------------------------------------------
if(AUTOTERMINAL_BUILD_TESTS)
    add_subdirectory(tests)
endif()