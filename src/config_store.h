#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "logger.h"

namespace autoterminal {

struct Hotkey {
    UINT modifiers = 0;   // MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN | MOD_NOREPEAT
    UINT vk = 0;          // virtual key code
};

// Per-process tiling mode. Grid is the classic matrix; Stack is a single
// equal-height column (1 col x N rows); Monocle places every window at the
// full monitor rect (overlapping — cycle via the taskbar / Alt+Tab).
enum class LayoutMode : int { Grid = 0, Stack = 1, Monocle = 2 };

struct Config {
    std::vector<std::wstring> process_names = {L"WindowsTerminal.exe"};
    // Per-process layout mode / monitor, lockstep with process_names by index.
    // A shorter (or empty) vector means "inherit": Grid for layout, and the
    // global target_monitor for monitor. This keeps bare-array configs (which
    // only populate process_names) backward-compatible.
    std::vector<LayoutMode>   process_layouts;
    std::vector<std::wstring> process_monitors;
    std::wstring target_monitor;          // friendly name or GDI name; empty = primary
    Hotkey hotkey_tile       {MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'T'};
    Hotkey hotkey_toggle_pause{MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'T'};
    Hotkey hotkey_tile_specific;   // tiles only the first configured process; disabled until set (vk=0)
    bool autostart = false;
    int autostart_delay = 0;   // seconds to suppress auto-tile after an autostart launch (0 = off)
    LogLevel log_level = LogLevel::Info;
    int padding = 0;
};

// Layout mode for process index `i`, defaulting to Grid when unset/short.
inline LayoutMode layout_for(const Config& c, size_t i) {
    return (i < c.process_layouts.size()) ? c.process_layouts[i] : LayoutMode::Grid;
}

// Monitor id for process index `i`, defaulting to "" (inherit target_monitor)
// when unset/short.
inline std::wstring monitor_for(const Config& c, size_t i) {
    return (i < c.process_monitors.size()) ? c.process_monitors[i] : std::wstring{};
}

// "grid" / "stack" / "monocle" <-> LayoutMode.
LayoutMode parse_layout_mode(std::string_view s);
std::string_view layout_mode_name(LayoutMode m);

// Returns the directory %APPDATA%\AutoTerminal, creating it if necessary.
std::filesystem::path config_dir();

// Returns the full path to config.toml inside config_dir().
std::filesystem::path config_path();

// Load config from `path`. If the file doesn't exist, write defaults to it
// and return them. On parse error, returns nullopt.
std::optional<Config> load_config(const std::filesystem::path& path);

// Save the current config (used when toggling autostart from the tray).
void save_config(const std::filesystem::path& path, const Config& cfg);

// Parse a string like "Ctrl+Alt+T" or "Ctrl+Shift+F5". Returns nullopt on error.
std::optional<Hotkey> parse_hotkey(std::wstring_view text);

// Format a hotkey back to its string form (round-trip friendly).
std::wstring format_hotkey(const Hotkey& hk);

} // namespace autoterminal