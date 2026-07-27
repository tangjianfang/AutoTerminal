#pragma once

#include <windows.h>

#include <functional>

namespace autoterminal {

// Public message IDs posted into the EventSource's hidden window.
inline constexpr UINT WM_AT_TILE_NOW     = WM_USER + 100;
inline constexpr UINT WM_AT_TOGGLE_PAUSE = WM_USER + 101;
inline constexpr UINT WM_AT_RELOAD       = WM_USER + 102;
inline constexpr UINT WM_AT_EXIT         = WM_USER + 103;

// Single-instance named mutex (per-user session).
inline constexpr wchar_t kSingletonMutexName[]    = L"Local\\AutoTerminal.singleton.v1";

// Hidden message-only window class name.
inline constexpr wchar_t kMessageWindowClass[]    = L"AutoTerminal.MessageWindow.v1";

class EventSource {
public:
    EventSource();
    ~EventSource();

    EventSource(const EventSource&) = delete;
    EventSource& operator=(const EventSource&) = delete;

    enum class Command { TileNow, TogglePause, Reload, Exit, TileSpecific };

    using TileCallback    = std::function<void()>;
    using ReloadCallback  = std::function<void()>;
    using PauseCallback   = std::function<void(bool paused)>;
    using CommandCallback = std::function<void(Command)>;

    bool start();
    void stop();

    void set_tile_callback(TileCallback cb);
    void set_reload_callback(ReloadCallback cb);
    void set_pause_callback(PauseCallback cb);
    void set_command_callback(CommandCallback cb);

    // Called from outside (the tray menu / hotkey dispatch site).
    void handle_hotkey(int id);

    bool paused() const { return paused_; }
    void set_paused(bool p);
    HWND hwnd() const { return hwnd_; }
    HINSTANCE hinstance() const { return hinstance_; }

    void post_tile_request();
    void toggle_pause();
    void request_reload();
    void request_exit();

    // Suppress WinEvent-driven auto-tiling for `seconds` after an autostart
    // launch, then fire one catch-up tile. Explicit Tile-now requests
    // (hotkey/tray) still tile immediately during the delay.
    void arm_startup_delay(int seconds);

private:
    static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK win_event_proc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG,
                                        DWORD, DWORD);

    void arm_debounce();
    void fire_tile_now();
    void fire_pause();

    // The single running instance — win_event_proc uses this to post messages.
    static EventSource* instance_;
    static EventSource* current_instance() { return instance_; }

    HWND hwnd_ = nullptr;
    HINSTANCE hinstance_ = nullptr;
    HWINEVENTHOOK hook_create_  = nullptr;
    HWINEVENTHOOK hook_destroy_ = nullptr;
    HWINEVENTHOOK hook_loc_     = nullptr;
    UINT_PTR debounce_timer_id_ = 0;
    UINT_PTR startup_timer_id_  = 0;
    TileCallback    tile_cb_;
    ReloadCallback  reload_cb_;
    PauseCallback   pause_cb_;
    CommandCallback cmd_cb_;
    bool paused_ = false;
    bool startup_delay_active_ = false;
};

} // namespace autoterminal