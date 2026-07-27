cmake_minimum_required(VERSION 3.20)
project(AutoTerminal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
        _CRT_SECURE_NO_WARNINGS TOML_EXCEPTIONS=1)
    # Static-link the MSVC runtime so AutoTerminal.exe does not require the
    # Visual C++ Redistributable on the target machine. /MT adds ~600 KB
    # but pays for itself by removing a deployment prerequisite. Both
    # autoterminal_core and the AutoTerminal executable opt in.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- third-party ----------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# toml++ for config.toml parsing.
#
# Default to a network clone, but allow a developer to fall back to a
# pre-staged source tree when the build host can't reach github.com
# (e.g. behind a captive portal). Set the cache var to point at a
# unpacked tomlplusplus v3.4.0 source root and re-run configure.
if(DEFINED FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS)
    message(STATUS "tomlplusplus: using FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=${FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS}")
    FetchContent_Declare(tomlplusplus SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS}")
else()
    FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG        v3.4.0
        GIT_SHALLOW    TRUE
    )
endif()
FetchContent_MakeAvailable(tomlplusplus)

# NativeFrameUI for themed, DPI-aware controls (Button / CheckBox / StaticText /
# Edit / ComboBox / Menu + Window subclassing). Consumed via FetchContent's
# SOURCE_DIR form so we pick up local edits on a clean rebuild without cloning
# from a remote — switch to GIT_REPOSITORY once the sibling repo is published.
FetchContent_Declare(
    NativeFrameUI
    SOURCE_DIR C:/tjf/github/NativeFrameUI
)
FetchContent_MakeAvailable(NativeFrameUI)

option(AUTOTERMINAL_BUILD_TESTS "Build unit tests" ON)
if(AUTOTERMINAL_BUILD_TESTS)
    enable_testing()
    if(DEFINED FETCHCONTENT_SOURCE_DIR_GOOGLETEST)
        message(STATUS "googletest: using FETCHCONTENT_SOURCE_DIR_GOOGLETEST=${FETCHCONTENT_SOURCE_DIR_GOOGLETEST}")
        FetchContent_Declare(googletest SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_GOOGLETEST}")
    else()
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.15.2
            GIT_SHALLOW    TRUE
        )
    endif()
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
    src/settings_dialog.cpp
    src/tray_menu.cpp
    src/about_dialog.cpp
    src/process_lister.cpp
    src/preview_overlay.cpp
)
target_include_directories(autoterminal_core PUBLIC src)
target_link_libraries(autoterminal_core PUBLIC
    tomlplusplus::tomlplusplus
    # NativeFrameUI — per-component link (only the controls we actually use):
    NativeFrameUI::nfui_core          # Application, Window, Control base, Theme
    NativeFrameUI::nfui_theme         # ThemePalette
    NativeFrameUI::nfui_window        # Window subclassing + wnd_proc
    NativeFrameUI::nfui_button        # Owner-draw Button
    NativeFrameUI::nfui_checkbox      # Owner-draw CheckBox
    NativeFrameUI::nfui_text          # StaticText labels
    NativeFrameUI::nfui_listbox       # ComboBox (monitor / log-level dropdowns)
    NativeFrameUI::nfui_menu          # Menu builder for the tray popup
)
if(WIN32)
    target_link_libraries(autoterminal_core PUBLIC
        User32 Gdi32 Shell32 Advapi32 SetupApi
        Ole32 Propsys Dwmapi Comctl32 Comdlg32)
endif()

# ---- main executable ------------------------------------------------------
# Embed the PerMonitorV2-aware application manifest so NativeFrameUI's
# DpiScale / dpi_of() helpers get reliable per-monitor DPI on multi-DPI setups.
# The .rc compiles into a .res via rc.exe; CMake links it into the executable
# so the taskbar / ALT-TAB / Explorer thumbnail all share app.ico.
add_executable(AutoTerminal WIN32
    src/main.cpp
    src/app.manifest
    src/app.rc
)
target_link_libraries(AutoTerminal PRIVATE autoterminal_core)

# ---- tests ----------------------------------------------------------------
if(AUTOTERMINAL_BUILD_TESTS)
    add_subdirectory(tests)
endif()