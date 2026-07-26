# AutoTerminal — Design Specification

**Date:** 2026-07-27
**Status:** Approved, in implementation
**Author:** Brainstorming session (user-approved)

## Purpose

A Windows background tool that watches all open terminal windows and arranges them in a dynamic best-fit grid on a chosen display. Re-tiles automatically when terminals open/close/move, when the display layout changes, or when the user presses a hotkey. Also exposes a tray menu for manual control and configuration.

## Goals

- **Zero-friction tiling:** open a terminal and it lands in the grid.
- **Stable monitor identity:** identify the target display by EDID-friendly name, not by hotplug index.
- **Predictable layout:** dynamic grid sized to window count, no manual rows/cols math.
- **Self-contained:** single `AutoTerminal.exe` with TOML config in `%APPDATA%`.

## Non-Goals

- Per-window layouts or saved "workspaces" (v1).
- Multi-monitor distribution (one monitor per session).
- Supporting non-terminal windows in v1.
- Running as a Windows Service.

## High-Level Architecture

A user-mode EXE (no service) with a tray icon and message-only window hosting event hooks.

```
   ┌────────────┐    ┌────────────┐    ┌────────────┐
   │ EventSource│───▶│ TileEngine │───▶│ WindowMgr  │
   └────────────┘    └────────────┘    └────────────┘
        │                  ▲
        ▼                  │
   ┌────────────┐    ┌────────────┐    ┌────────────┐
   │ ConfigStore│    │ MonitorIdx │    │ UIBridge   │
   └────────────┘    └────────────┘    └────────────┘
```

| Component | Responsibility |
|-----------|----------------|
| EventSource | Win32 event hooks (`SetWinEventHook`), display-change msg, hotkeys. Debounces and triggers one re-tile per burst. |
| TileEngine | Pure function: `(monitorRect, windowCount, padding) → (rows, cols, cellRects)`. Unit-testable. |
| WindowManager | `EnumWindows` + process-name filter + state-restore + `SetWindowPos`. |
| MonitorIndex | Cache `(hMonitor → friendlyName)` from EDID; falls back to GDI device name. |
| ConfigStore | Read/write `%APPDATA%\AutoTerminal\config.toml`. |
| UIBridge | Tray icon + context menu + global hotkeys + autostart toggle. |
| Logger | Rolling log to `%APPDATA%\AutoTerminal\autoterminal.log`. |

## Behavior

### Trigger model

The user wants a **background-resident** daemon that auto-tiles on every relevant change.

| Event | Source | Effect |
|-------|--------|--------|
| Terminal window opened | `EVENT_OBJECT_CREATE` (process name match) | Re-tile |
| Terminal window closed | `EVENT_OBJECT_DESTROY` | Re-tile |
| Any tracked window moved/resized | `EVENT_OBJECT_LOCATIONCHANGE` | Re-tile (snap back) |
| Display config changed | `WM_DISPLAYCHANGE` | Re-tile |
| Hotkey `Ctrl+Alt+T` | `RegisterHotKey` | Re-tile (no debounce) |
| Hotkey `Ctrl+Alt+Shift+T` | `RegisterHotKey` | Toggle pause |
| Tray menu "Tile now" | WM_COMMAND | Re-tile (no debounce) |
| Tray menu "Pause" | WM_COMMAND | Toggle pause |
| Tray menu "Exit" | WM_COMMAND | Quit |

### Debouncing

All Win32 event hooks feed a 150 ms debounce timer. Bursts (e.g. opening 5 tabs in Windows Terminal) coalesce into one re-tile. Manual triggers bypass debounce.

### Layout algorithm

Given `N` terminal windows and a monitor rect `(W, H)`:

1. Pick `rows = floor(sqrt(N))` if `rows² >= N`, else `rows = ceil(sqrt(N))`.
2. `cols = ceil(N / rows)`.
3. To minimize wasted space, also try the transpose `(cols, rows)` and pick the variant whose cell aspect ratio is closer to `W/H`.
4. Each cell: `cellW = W / cols`, `cellH = H / rows`. Last row gets only the cells needed for the remaining windows; trailing cells stay empty.

### Window state handling

- Hidden (`WS_VISIBLE` off, or `IsWindowVisible` false) → **skip**.
- Minimized (`WS_MINIMIZE`) → `ShowWindow(SW_RESTORE)` first.
- Maximized (`WS_MAXIMIZE`) → `ShowWindow(SW_RESTORE)` first.
- Z-order and focus: **not changed**. `SetWindowPos` is called without `SWP_NOZORDER`/`SWP_NOACTIVATE` only if needed; otherwise flags preserve current focus.

### Margins/padding

`padding = 0` by default. Configurable in TOML.

### Manual controls

- **Tray icon:** right-click → menu.
- **Hotkeys:** registered globally via `RegisterHotKey`. Defaults `Ctrl+Alt+T` (tile now) and `Ctrl+Alt+Shift+T` (toggle pause). Configurable.

## Configuration

Path: `%APPDATA%\AutoTerminal\config.toml`.

```toml
[targets]
process_names = ["WindowsTerminal.exe"]
target_monitor = ""        # EDID friendly name; empty = primary

[hotkeys]
tile_now = "Ctrl+Alt+T"
toggle_pause = "Ctrl+Alt+Shift+T"

[ui]
autostart = false
log_level = "info"         # "debug" | "info" | "warn" | "error"

[layout]
padding = 0
```

### Hotkey parsing

Simple subset: `<modifier>* + <key>`, modifiers in `{Ctrl, Alt, Shift, Win}`, key is a single character or named VK (`F1`-`F12`, `Left`, `Right`, `Up`, `Down`, `Space`, `Tab`, `Enter`, `Esc`).

## Lifecycle

- Single instance via `CreateMutex` (named `Global\AutoTerminal.singleton.v1`). Second invocation sends a custom message to the first instance to reload config, then exits.
- **Autostart:** controlled by a tray menu checkbox. Implementation: write/remove `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\AutoTerminal` value. Default: off.
- Exit: tray "Exit" or `WM_CLOSE` on the message-only window.

## Error Handling

| Situation | Behavior |
|-----------|----------|
| Target monitor not found | Fall back to primary monitor; log warning; tray balloon. |
| Config missing | Write defaults, continue. |
| Config parse error | Show tray balloon with error, quit. |
| Win32 API failure on a single window | Log + skip; continue with others. |
| Display unplugged during tile | Layout for last-known monitor rect; next `WM_DISPLAYCHANGE` re-resolves. |

## Project Structure

```
AutoTerminal/
├── CMakeLists.txt
├── readme.md
├── docs/superpowers/specs/
│   └── 2026-07-27-autoterminal-design.md   # this file
├── include/                                # public headers (mostly empty; modules own their headers)
├── src/
│   ├── main.cpp
│   ├── logger.{h,cpp}
│   ├── tile_engine.{h,cpp}
│   ├── monitor_index.{h,cpp}
│   ├── config_store.{h,cpp}
│   ├── window_manager.{h,cpp}
│   ├── event_source.{h,cpp}
│   └── ui_bridge.{h,cpp}
├── tests/
│   ├── CMakeLists.txt
│   ├── test_tile_engine.cpp
│   ├── test_config_store.cpp
│   └── test_hotkey.cpp
└── third_party/
    └── tomlplusplus/         # CMake FetchContent
```

## Dependencies

- **tomlplusplus** (`https://github.com/marzer/tomlplusplus`) — TOML parser/serializer, fetched via CMake `FetchContent`.
- **GoogleTest** — unit tests, fetched via CMake `FetchContent`.

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

Output: `build/AutoTerminal.exe`. Target: Windows 10+ x64.

## Testing Strategy

| Layer | Method |
|-------|--------|
| TileEngine | Pure-function unit tests covering N=1..20, several monitor aspect ratios. |
| ConfigStore | Round-trip parse tests for valid/invalid TOML. |
| Hotkey parsing | Unit tests for modifier combos. |
| WindowManager | Manual smoke test; integration test fake window helper. |

## Defaults Picked (best-judgment, per user)

| Decision | Value |
|----------|-------|
| Config format | TOML (toml++) |
| Focus policy | Preserve current focus |
| Z-order | Preserve |
| Debounce window | 150 ms |
| Hotkey "Tile now" | Ctrl+Alt+T |
| Hotkey "Pause" | Ctrl+Alt+Shift+T |
| Log location | `%APPDATA%\AutoTerminal\autoterminal.log` |
| Log level default | info |
| Hidden windows | Skip |
| Autostart default | Off |
| Single-instance mutex | `Global\AutoTerminal.singleton.v1` |