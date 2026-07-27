#include "monitor_index.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <string>

namespace autoterminal {

namespace {

struct EnumCtx {
    std::vector<MonitorInfo>* out;
};

// Callback for EnumDisplayMonitors. Captures the GDI device name and rect.
BOOL CALLBACK monitor_enum_proc(HMONITOR h, HDC, LPRECT, LPARAM lparam) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lparam);

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(h, &info)) return TRUE;

    MonitorInfo m;
    m.rect.x = info.rcMonitor.left;
    m.rect.y = info.rcMonitor.top;
    m.rect.w = info.rcMonitor.right  - info.rcMonitor.left;
    m.rect.h = info.rcMonitor.bottom - info.rcMonitor.top;
    m.gdi_name = info.szDevice;
    m.friendly_name = info.szDevice;        // may be upgraded below
    m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    ctx->out->push_back(std::move(m));
    return TRUE;
}

// Walk QueryDisplayConfig to attach EDID-derived friendly names.
// The result is a best-effort overlay onto `monitors`: it overwrites
// friendly_name whenever the target reports a non-empty name.
void overlay_edid_friendly_names(std::vector<MonitorInfo>& monitors) {
    UINT32 num_paths = 0, num_modes = 0;
    LONG rc = GetDisplayConfigBufferSizes(QDC_DATABASE_CURRENT, &num_paths, &num_modes);
    if (rc != ERROR_SUCCESS || num_paths == 0) return;

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(num_paths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(num_modes);
    rc = QueryDisplayConfig(QDC_DATABASE_CURRENT, &num_paths, paths.data(),
                            &num_modes, modes.data(), nullptr);
    if (rc != ERROR_SUCCESS) return;

    // Build a map: gdi_name -> friendly_name from the source/target pairs.
    for (const auto& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type   = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size   = sizeof(src);
        src.header.adapterId = path.sourceInfo.adapterId;
        src.header.id     = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;
        if (src.viewGdiDeviceName[0] == L'\0') continue;

        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt{};
        tgt.header.type   = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt.header.size   = sizeof(tgt);
        tgt.header.adapterId = path.targetInfo.adapterId;
        tgt.header.id     = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tgt.header) != ERROR_SUCCESS) continue;
        if (tgt.monitorFriendlyDeviceName[0] == L'\0') continue;

        std::wstring gdi(src.viewGdiDeviceName);
        std::wstring friendly(tgt.monitorFriendlyDeviceName);
        for (auto& m : monitors) {
            if (m.gdi_name == gdi) {
                m.friendly_name = friendly;
                break;
            }
        }
    }
}

} // namespace

std::vector<MonitorInfo> enumerate_monitors() {
    std::vector<MonitorInfo> out;
    EnumCtx ctx{&out};
    EnumDisplayMonitors(nullptr, nullptr, monitor_enum_proc,
                        reinterpret_cast<LPARAM>(&ctx));
    overlay_edid_friendly_names(out);
    return out;
}

const MonitorInfo* resolve_monitor(const std::vector<MonitorInfo>& monitors,
                                   const std::wstring& identifier) {
    if (identifier.empty()) {
        for (const auto& m : monitors) if (m.primary) return &m;
        return monitors.empty() ? nullptr : &monitors.front();
    }
    for (const auto& m : monitors) if (m.friendly_name == identifier) return &m;
    for (const auto& m : monitors) if (m.gdi_name == identifier) return &m;
    return nullptr;
}

std::string monitor_log_label(const std::wstring& s) {
    if (s.empty()) return "(primary)";
    std::string out;
    out.reserve(s.size());
    for (wchar_t c : s) out.push_back(static_cast<char>(c & 0x7F));
    return out;
}

std::vector<PlannedRule> plan_layouts(const Config& cfg,
                                      const std::vector<MonitorInfo>& monitors,
                                      const std::vector<size_t>& rule_indices,
                                      const std::function<int(size_t)>& count_fn) {
    std::vector<PlannedRule> out;
    out.reserve(rule_indices.size());
    for (size_t i : rule_indices) {
        PlannedRule p;
        p.rule_index = i;
        if (i < cfg.process_names.size()) {
            p.name = cfg.process_names[i];
            p.mode = layout_for(cfg, i);

            // Monitor fallback chain — must match perform_tile_locked_rules
            // exactly so the preview shows what an actual tile would do.
            std::wstring mon_id = monitor_for(cfg, i);
            if (mon_id.empty()) mon_id = cfg.target_monitor;
            p.requested_monitor_id = mon_id;
            const MonitorInfo* target = resolve_monitor(monitors, mon_id);
            if (!target) {
                // First lookup failed → try primary. Flag it so the caller can
                // emit the same "falling back to primary" WARN as the real path
                // (matches perform_tile_locked_rules exactly, including the
                // zero-monitors edge where primary also fails).
                p.monitor_fell_back = true;
                target = resolve_monitor(monitors, L"");
            }
            p.monitor = target;

            int count = count_fn ? count_fn(i) : 0;
            p.window_count = count;
            if (target && count > 0) {
                p.layout = compute_layout(target->rect, count, cfg.padding, p.mode);
            }
        }
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace autoterminal