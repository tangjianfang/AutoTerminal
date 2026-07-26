#pragma once

#include "config_store.h"

#include <functional>

namespace autoterminal {

// Callbacks fired by the modeless settings window. Aggregate-initializable;
// designated-init syntax in main.cpp:
//   SettingsCallbacks{ .on_apply = [&](const Config& c){ ... } }
struct SettingsCallbacks {
    // Called when the user clicks "Apply". `cfg` contains the new values.
    std::function<void(const Config&)> on_apply;
    // Called when the user clicks "Cancel" or closes the window. Defaults to no-op.
    std::function<void()> on_close = []{};
};

// Create the settings window (hidden by default). Returns the HWND or nullptr
// on failure. The window is a top-level WS_OVERLAPPED frame.
HWND create_settings_window(HINSTANCE hinst,
                            Config initial,
                            SettingsCallbacks cbs);

void show_settings_window(HWND hwnd, bool show);
bool settings_window_visible(HWND hwnd);

// Forward the WM_KEYDOWN of a hotkey-capture moment through here: parses
// the (vk, mods) into Hotkey, persists it into the dialog's pending state,
// and updates the label. Returns true if the message was consumed.
bool try_settings_capture_key(HWND settings_hwnd, WPARAM vk);

} // namespace autoterminal
