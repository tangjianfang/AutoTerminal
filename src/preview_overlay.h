#pragma once

// Tiling preview overlay — a pure Win32 layered-window visualization of where
// each configured process WOULD tile. It does NOT move any windows; it just
// draws translucent cells over the affected monitors for a few seconds.
//
// Implementation is deliberately free of NativeFrameUI so this feature can ship
// without coupling to the sibling repo's build. All rendering uses
// UpdateLayeredWindow with a premultiplied 32bpp DIB.

#include <windows.h>

#include <vector>

#include "config_store.h"
#include "monitor_index.h"

namespace autoterminal {

class OverlayManager {
public:
    OverlayManager() = default;
    ~OverlayManager();

    OverlayManager(const OverlayManager&) = delete;
    OverlayManager& operator=(const OverlayManager&) = delete;

    // The EventSource's hidden window owns the auto-hide timer and dispatches
    // WM_DISPLAYCHANGE / hide callbacks; the overlay paints into its own
    // per-monitor top-level windows.
    void set_event_hwnd(HWND hwnd) { event_hwnd_ = hwnd; }

    // Compute the plan for every configured rule (live window counts) and draw
    // translucent cells over each affected monitor. Idempotent: a second show
    // while visible tears down the prior windows first. Arms an auto-hide timer
    // on event_hwnd_.
    void show(const Config& cfg);

    // Tear down any visible overlay windows and kill the auto-hide timer.
    // Safe to call when nothing is shown.
    void hide();

    // Toggle: show if hidden, hide if shown.
    void toggle(const Config& cfg) { if (visible_) hide(); else show(cfg); }

    bool visible() const { return visible_; }

    // Destroy any lingering windows (called from main on shutdown). Implies
    // hide(); safe to call repeatedly.
    void shutdown();

private:
    void destroy_all();

    HWND event_hwnd_ = nullptr;
    std::vector<HWND> windows_;
    bool visible_ = false;
};

} // namespace autoterminal