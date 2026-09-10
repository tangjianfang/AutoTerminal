# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

AutoTerminal is a Windows background tool (C++20, raw Win32, no .NET) that auto-tiles terminal windows into a best-fit grid on a chosen monitor. It runs as a tray-icon daemon with global hotkeys, a settings dialog, and a TOML config at `%APPDATA%\AutoTerminal\config.toml`.

## Build & test

```bat
build.bat                :: configure + build (MSVC + Ninja, Release) → build\AutoTerminal.exe
build.bat clean          :: wipe build\ first
build\tests\autoterminal_tests.exe                        :: run all unit tests
build\tests\autoterminal_tests.exe --gtest_filter=Tile.*  :: run one test suite
ctest --test-dir build                                    :: alternative via ctest
```

- MSVC 14.3x+ and CMake 3.20+ required; `build.bat` calls `vcvars64.bat` itself.
- Third-party deps (toml++ v3.4.0, GoogleTest v1.15.2) are fetched by CMake FetchContent. Offline fallbacks: set `FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS` / `FETCHCONTENT_SOURCE_DIR_GOOGLETEST` to pre-staged source roots.
- **NativeFrameUI is a hard dependency from a local sibling repo** (`SOURCE_DIR C:/tjf/github/NativeFrameUI` in CMakeLists.txt). The build fails without it. UI controls are linked per-component (`NativeFrameUI::nfui_button`, `nfui_menu`, etc.).
- MSVC runtime is statically linked (`/MT`) so the exe needs no VC++ redistributable.
- Run: `build\AutoTerminal.exe` (shows Settings on first launch) or `--silent` (the autostart form). Logs: `%APPDATA%\AutoTerminal\autoterminal.log` (mirrored to DebugView).

## Architecture

Pipeline (see `docs/superpowers/specs/2026-07-27-autoterminal-design.md` for the full design):

```
EventSource  →  TileEngine (pure)  →  WindowManager
(Win32 hooks, WM_DISPLAYCHANGE,      (EnumWindows → filter
 hotkeys; debounced 150 ms)           by process → SetWindowPos)
```

- **`main.cpp`** — owns global state behind `g_state_mutex`, wires all callbacks together, implements `perform_tile_locked_rules` (the multi-rule tile pass: collect windows per rule → `plan_layouts` → apply per plan).
- **`event_source.*`** — hidden message-only window (`kMessageWindowClass`), Win32 event hooks, `WM_AT_*` custom messages, singleton mutex, preview-overlay timer.
- **`tile_engine.*`** — pure layout math: `compute_layout(monitor, count, padding, mode)` picks the grid whose cell aspect best matches the monitor; modes are Grid / Stack / Monocle. Also `plan_layouts` for multi-process plans. No Win32 state — this is the most-tested code.
- **`window_manager.*`** — `collect_terminal_windows` (EnumWindows + process-name suffix match) and `apply_layout` (SetWindowPos into cells).
- **`config_store.*`** — TOML read/write via toml++; `Config` struct with `process_names` plus lockstep vectors `process_layouts` / `process_monitors` (shorter vector = "inherit", keeping bare-array configs backward-compatible).
- **`monitor_index.*`** — EDID-friendly monitor-name resolution.
- **`ui_bridge.*`** — tray icon, RegisterHotKey, autostart (`HKCU\...\Run`) toggle.
- **`settings_dialog.cpp`** (largest file) — visual settings UI built on NativeFrameUI; hotkey click-to-capture, monitor picker, per-process list with rename/reorder, Export/Import.
- **`preview_overlay.*`** — pure Win32 layered window showing the tiling preview.
- **`tray_menu.cpp`, `about_dialog.cpp`, `process_lister.*`, `logger.*`** — support code.

Key invariant: window lists in the tile pass are keyed by **rule index** (index into `process_names`), not position within the selected subset — non-contiguous rule selections must keep mapping correctly.

### Gotchas recorded in the code

- The tray popup needs a real (invisible, 0x0) helper window as the foreground target — a `HWND_MESSAGE` window can't take foreground on Win10/11.
- `build.bat` resolves `ninja.exe` explicitly because depot_tools ships a PATH-shadowing extension-less `ninja` bash script that confuses CMake's `find_program`.
- Settings dialog must stay DPI-aware (PerMonitorV2 manifest embedded via `app.manifest`); past bugs involved clipping at non-100% DPI.

## Diagnostic scripts

`scripts/*.ps1` (run with `powershell -ExecutionPolicy Bypass -File scripts\<name>.ps1`): `probe_windows.ps1` (message/settings window + control counts), `probe_settings_pos.ps1` (dialog rect + foreground), `simulate_rightclick.ps1` (posts `WM_AT_TRAYICON`/`WM_RBUTTONUP` to test tray dispatch without clicking). Useful for verifying tray/UI behavior of the running daemon.

**Verification policy:** do NOT use screenshot/image recognition for automated verification. Verify UI via geometry probes (`scripts/verify_settings_layout.ps1` asserts containment, no-overlap, and two-column separation over every child control), behavior via `%APPDATA%\AutoTerminal\autoterminal.log`, and logic via unit tests + code analysis.
