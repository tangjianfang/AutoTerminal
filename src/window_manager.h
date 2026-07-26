#pragma once

#include <string>
#include <vector>

#include "config_store.h"
#include "monitor_index.h"
#include "tile_engine.h"

namespace autoterminal {

// One top-level window we're tracking.
struct WindowEntry {
    HWND hwnd;
    DWORD pid;
    std::wstring process_name;     // e.g. "WindowsTerminal.exe"
};

// Enumerate top-level windows whose process name matches `allowed_processes`
// and which are currently visible.
std::vector<WindowEntry> collect_terminal_windows(
    const std::vector<std::wstring>& allowed_processes);

// Move every window in `windows` into the cells described by `layout`.
// Returns the number of windows successfully placed.
int apply_layout(const std::vector<WindowEntry>& windows, const Layout& layout);

} // namespace autoterminal