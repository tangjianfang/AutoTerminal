#pragma once

#include <string>
#include <vector>

#include "tile_engine.h"

namespace autoterminal {

struct MonitorInfo {
    Rect rect;                        // full virtual-screen rect of the monitor
    std::wstring gdi_name;            // "\\.\DISPLAY1"
    std::wstring friendly_name;       // EDID-derived, e.g. "Dell U2723QE"; may equal gdi_name
    bool primary = false;
};

// Re-enumerate all monitors and return them in EnumDisplayMonitors order.
std::vector<MonitorInfo> enumerate_monitors();

// Resolve a user-supplied monitor identifier against the current set:
//   1. exact match on friendly_name
//   2. exact match on gdi_name
//   3. empty input → primary monitor
// Returns nullptr if nothing matched AND input was non-empty.
const MonitorInfo* resolve_monitor(const std::vector<MonitorInfo>& monitors,
                                   const std::wstring& identifier);

// Returns a short, ASCII-friendly label for log messages.
std::string monitor_log_label(const std::wstring& s);

} // namespace autoterminal