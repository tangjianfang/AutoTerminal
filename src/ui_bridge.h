#pragma once

#include <functional>
#include <string>

#include <windows.h>

#include "config_store.h"

namespace autoterminal {

// Menu command IDs returned to host via the command callback.
enum TrayCommand {
    TrayCmdTileNow = 1,
    TrayCmdTogglePause,
    TrayCmdToggleAutostart,
    TrayCmdOpenConfig,
    TrayCmdReload,
    TrayCmdAbout,
    TrayCmdExit,
};

// Notification message delivered to the EventSource's hidden window.
inline constexpr UINT WM_AT_TRAYICON = WM_USER + 200;

class UIBridge {
public:
    UIBridge();
    ~UIBridge();

    UIBridge(const UIBridge&) = delete;
    UIBridge& operator=(const UIBridge&) = delete;

    // Install tray icon and register global hotkeys. `hwnd` is the hidden
    // EventSource window that will receive WM_AT_TRAYICON.
    bool init(HWND hwnd);

    void shutdown();

    // Apply runtime settings: hotkeys (re-register), and the checked state
    // of the autostart menu item.
    void apply_config(const Config& cfg);

    // Open config file in the user's default editor.
    static void open_config_file();

    // Toggle the autostart registry entry and reflect the change.
    static bool set_autostart(bool enable);

    using CommandCallback = std::function<void(TrayCommand)>;
    void set_command_callback(CommandCallback cb) { cmd_cb_ = std::move(cb); }

    // Allow the EventSource's wnd_proc to dispatch tray messages.
    void on_tray_message(HWND hwnd, LPARAM lparam);

private:
    void register_hotkeys();
    void unregister_hotkeys();

    HWND hwnd_ = nullptr;
    bool added_ = false;
    int hotkey_id_tile_ = 1;
    int hotkey_id_pause_ = 2;
    Config current_config_{};
    CommandCallback cmd_cb_;
};

} // namespace autoterminal