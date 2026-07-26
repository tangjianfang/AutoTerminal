# AutoTerminal

A Windows background tool that arranges your open terminal windows into a dynamic best-fit grid on a chosen display. New terminals slide into place; closed ones close the gap; dragged windows snap back.

Targets Windows Terminal out of the box, but the process-name filter is configurable. Written in C++20 with raw Win32 (no .NET runtime required).

## Features

- **Auto-tile** on window create / destroy / move / resize, debounced 150 ms.
- **Display-aware**: re-tiles when monitors change or get unplugged.
- **Aspect-aware grid**: cells match the target monitor's aspect ratio.
- **Tray icon + global hotkeys**: pause/resume and one-shot tile from anywhere.
- **Zero-config start**: writes `%APPDATA%\AutoTerminal\config.toml` with sane defaults on first run.
- **TOML config** in `%APPDATA%\AutoTerminal\config.toml`.

## Build

Requires MSVC 14.3x+ and CMake 3.20+ (Ninja recommended). Third-party deps (toml++, GoogleTest) are fetched automatically by CMake.

```bat
build.bat
```

That produces `build\AutoTerminal.exe`.

## Run

### Manual (double-click from Explorer)

```bat
build\AutoTerminal.exe
```

The first launch writes a default `config.toml` and immediately shows the **Settings window** so you see something happen. Edit anything you need (the default hotkeys and target monitor are sensible), click **Apply**, and the window hides — the daemon keeps running with the tray icon for hotkey access.

### Silent (used by autostart at logon)

```bat
build\AutoTerminal.exe --silent
```

Same daemon, but the Settings window stays hidden. The autostart registry entry uses this form so logon doesn't pop a window in front of the user.

### Tray icon

Right-click the tray icon for:

- **Tile now** — run one re-tile pass immediately
- **Pause auto-tile** — stop responding to events (manual triggers still work)
- **Start with Windows** — toggle the `HKCU\...\Run` autostart entry
- **Open config file...** — open `config.toml` in Notepad
- **Settings...** — open the visual settings window (hotkey capture, monitor picker, padding, autostart, log level)
- **Reload config** — re-read the config without relaunching
- **About** / **Exit**

The Settings window lets you edit every field in `config.toml` without hand-editing TOML. Hotkey fields support click-to-capture: click *Capture*, press the desired chord (Esc cancels), then **Apply**.

Default hotkeys: `Ctrl+Alt+T` (tile now), `Ctrl+Alt+Shift+T` (pause).

## Configuration

Edit `%APPDATA%\AutoTerminal\config.toml`:

```toml
[targets]
process_names = ["WindowsTerminal.exe"]   # any process name; case-insensitive suffix match
target_monitor = ""                       # EDID friendly name; empty = primary monitor

[hotkeys]
tile_now      = "Ctrl+Alt+T"
toggle_pause  = "Ctrl+Alt+Shift+T"

[ui]
autostart  = false
log_level  = "info"                       # "debug" | "info" | "warn" | "error"

[layout]
padding = 0                               # pixels of margin/edge; 0 = edge-to-edge
```

To target other terminals, set `process_names` to e.g. `["WindowsTerminal.exe", "pwsh.exe", "cmd.exe"]`.

## Architecture

```
EventSource (Win32 hooks + WM_DISPLAYCHANGE)
    │  debounced 150 ms
    ▼
TileEngine (pure function: monitor × N → cells)
    │
    ▼
WindowManager (EnumWindows → filter → SetWindowPos)
```

Side channels:

- `MonitorIndex` (EDID-friendly name resolution)
- `ConfigStore` (TOML read/write)
- `UIBridge` (tray icon + RegisterHotKey + autostart toggle)

See `docs/superpowers/specs/2026-07-27-autoterminal-design.md` for the full design.

## Test

```bat
build\tests\autoterminal_tests.exe
```

30 unit tests cover the layout algorithm, config parsing, hotkey parsing, and monitor resolution.

## Logs

`%APPDATA%\AutoTerminal\autoterminal.log` (also mirrored to DebugView via `OutputDebugString`).

## Diagnostic scripts

```bat
powershell -ExecutionPolicy Bypass -File scripts\probe_windows.ps1
powershell -ExecutionPolicy Bypass -File scripts\simulate_rightclick.ps1
```

`probe_windows.ps1` shows the message window, settings window, and child-control count. `simulate_rightclick.ps1` posts `WM_AT_TRAYICON`/`WM_RBUTTONUP` to the running message window — useful for confirming dispatch without clicking the tray.

## License

MIT (or your choice — none specified yet).