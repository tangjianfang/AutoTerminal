#pragma once

#include <functional>
#include <string>
#include <vector>

#include "config_store.h"
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

// A fully resolved tiling plan for one configured process rule. This is the
// read-only "what would happen if I tiled right now" view used by the preview
// overlay — it resolves the rule's monitor (with the same primary-fallback
// chain perform_tile uses) and computes the cell layout, but applies nothing.
//
// `monitor` is borrowed from the `monitors` vector passed to plan_layouts and
// is nullptr when the rule has no placeable monitor (out-of-range index, no
// monitors enumerated). `layout.cells` is empty when window_count <= 0 or the
// monitor didn't resolve, so callers can skip rules with nothing to draw.
struct PlannedRule {
    size_t rule_index = 0;
    std::wstring name;
    LayoutMode mode = LayoutMode::Grid;
    int window_count = 0;
    const MonitorInfo* monitor = nullptr;   // borrowed from the monitors vector
    std::wstring requested_monitor_id;       // the per-rule id before fallback
    bool monitor_fell_back = false;          // true if the first monitor lookup failed (we then tried primary)
    Layout layout;                           // empty cells when window_count<=0 or monitor==nullptr
};

// Build a tiling plan for the given rule indices. `count_fn(i)` returns the
// number of windows the i-th configured process currently has open (the
// preview passes a live collect_terminal_windows count; the production tile
// path passes the same). The fallback chain matches perform_tile exactly:
//   mon_id = monitor_for(cfg, i); if empty → cfg.target_monitor
//   target = resolve_monitor(monitors, mon_id)
//   if !target → target = resolve_monitor(monitors, L"")  (primary)
//   monitor_fell_back = !mon_id.empty() && first resolve failed
std::vector<PlannedRule> plan_layouts(const Config& cfg,
                                      const std::vector<MonitorInfo>& monitors,
                                      const std::vector<size_t>& rule_indices,
                                      const std::function<int(size_t)>& count_fn);

} // namespace autoterminal