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

struct Config {
    std::vector<std::wstring> process_names = {L"WindowsTerminal.exe"};
    std::wstring target_monitor;          // friendly name or GDI name; empty = primary
    Hotkey hotkey_tile       {MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'T'};
    Hotkey hotkey_toggle_pause{MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'T'};
    bool autostart = false;
    int autostart_delay = 0;   // seconds to suppress auto-tile after an autostart launch (0 = off)
    LogLevel log_level = LogLevel::Info;
    int padding = 0;
};

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