#include "event_source.h"

#include "logger.h"

#include <string>

namespace autoterminal {

namespace {

constexpr UINT EVENT_GLOBAL_CREATE  = EVENT_OBJECT_CREATE;
constexpr UINT EVENT_GLOBAL_DESTROY = EVENT_OBJECT_DESTROY;
constexpr UINT EVENT_GLOBAL_LOC     = EVENT_OBJECT_LOCATIONCHANGE;
constexpr UINT_PTR kDebounceTimerId     = 0xA70E0001;
constexpr UINT kDebounceMs              = 150;
constexpr UINT_PTR kStartupDelayTimerId = 0xA70E0002;

} // namespace

EventSource* EventSource::instance_ = nullptr;

EventSource::EventSource() = default;
EventSource::~EventSource() { stop(); }

bool EventSource::start() {
    if (hwnd_) return true;
    hinstance_ = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hinstance_;
    wc.lpszClassName = kMessageWindowClass;
    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            AT_LOG_ERROR("RegisterClassEx failed err=%lu", err);
            return false;
        }
    }

    hwnd_ = CreateWindowExW(0, kMessageWindowClass, L"AutoTerminal",
                            0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, hinstance_,
                            this);
    if (!hwnd_) {
        AT_LOG_ERROR("CreateWindowEx (message-only) failed");
        return false;
    }
    instance_ = this;

    hook_create_  = SetWinEventHook(EVENT_GLOBAL_CREATE,  EVENT_GLOBAL_CREATE,
                                    nullptr, win_event_proc, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    hook_destroy_ = SetWinEventHook(EVENT_GLOBAL_DESTROY, EVENT_GLOBAL_DESTROY,
                                    nullptr, win_event_proc, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    hook_loc_     = SetWinEventHook(EVENT_GLOBAL_LOC,     EVENT_GLOBAL_LOC,
                                    nullptr, win_event_proc, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    AT_LOG_INFO("EventSource started; hwnd=0x%p", hwnd_);
    return true;
}

void EventSource::stop() {
    if (hook_create_)  { UnhookWinEvent(hook_create_);  hook_create_  = nullptr; }
    if (hook_destroy_) { UnhookWinEvent(hook_destroy_); hook_destroy_ = nullptr; }
    if (hook_loc_)     { UnhookWinEvent(hook_loc_);     hook_loc_     = nullptr; }
    if (debounce_timer_id_) {
        KillTimer(hwnd_, debounce_timer_id_);
        debounce_timer_id_ = 0;
    }
    if (startup_timer_id_) {
        KillTimer(hwnd_, startup_timer_id_);
        startup_timer_id_ = 0;
    }
    KillTimer(hwnd_, kPreviewTimerId);   // drop any pending preview auto-hide
    startup_delay_active_ = false;
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (instance_ == this) instance_ = nullptr;
}

void EventSource::set_tile_callback(TileCallback cb)     { tile_cb_   = std::move(cb); }

void EventSource::set_paused(bool p) {
    if (paused_ == p) return;
    paused_ = p;
    if (pause_cb_) pause_cb_(paused_);
    AT_LOG_INFO(paused_ ? "Auto-tile paused" : "Auto-tile resumed");
}
void EventSource::set_reload_callback(ReloadCallback cb)  { reload_cb_ = std::move(cb); }
void EventSource::set_pause_callback(PauseCallback cb)    { pause_cb_  = std::move(cb); }
void EventSource::set_command_callback(CommandCallback cb) { cmd_cb_    = std::move(cb); }

void EventSource::handle_hotkey(int id) {
    if (!cmd_cb_) return;
    // Map IDs from UIBridge; if hotkey ID 1 → TileNow, 2 → TogglePause.
    switch (id) {
        case 1: cmd_cb_(Command::TileNow);    break;
        case 2: cmd_cb_(Command::TogglePause);break;
        case 3: cmd_cb_(Command::TileSpecific);break;
        case 4: cmd_cb_(Command::Preview);    break;
        default: break;
    }
}

void EventSource::post_tile_request() {
    if (!hwnd_) return;
    PostMessageW(hwnd_, WM_AT_TILE_NOW, 0, 0);
}
void EventSource::toggle_pause() {
    if (!hwnd_) return;
    PostMessageW(hwnd_, WM_AT_TOGGLE_PAUSE, 0, 0);
}
void EventSource::request_reload() {
    if (!hwnd_) return;
    PostMessageW(hwnd_, WM_AT_RELOAD, 0, 0);
}
void EventSource::request_exit() {
    if (!hwnd_) return;
    PostMessageW(hwnd_, WM_AT_EXIT, 0, 0);
}
void EventSource::post_preview_request() {
    if (!hwnd_) return;
    PostMessageW(hwnd_, WM_AT_PREVIEW, 0, 0);
}

void EventSource::set_displaychange_callback(DisplayChangeCallback cb) {
    displaychange_cb_ = std::move(cb);
}
void EventSource::set_preview_hide_callback(PreviewHideCallback cb) {
    preview_hide_cb_ = std::move(cb);
}

void EventSource::arm_startup_delay(int seconds) {
    if (seconds <= 0 || !hwnd_) return;
    if (startup_timer_id_) KillTimer(hwnd_, startup_timer_id_);
    startup_delay_active_ = true;
    startup_timer_id_ = SetTimer(hwnd_, kStartupDelayTimerId,
                                 static_cast<UINT>(seconds) * 1000u, nullptr);
    AT_LOG_INFO("Autostart startup delay armed: %d s", seconds);
}

void EventSource::arm_debounce() {
    if (paused_) return;
    if (startup_delay_active_) return;   // hold off auto-tile during the delay
    if (debounce_timer_id_) KillTimer(hwnd_, debounce_timer_id_);
    debounce_timer_id_ = SetTimer(hwnd_, kDebounceTimerId, kDebounceMs, nullptr);
}

void EventSource::fire_tile_now() {
    if (debounce_timer_id_) {
        KillTimer(hwnd_, debounce_timer_id_);
        debounce_timer_id_ = 0;
    }
    if (tile_cb_) tile_cb_();
}

void EventSource::fire_pause() {
    paused_ = !paused_;
    if (pause_cb_) pause_cb_(paused_);
    if (!paused_) fire_tile_now();
}

LRESULT CALLBACK EventSource::wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(l);
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }
    auto* self = reinterpret_cast<EventSource*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self || self != instance_) {
        return DefWindowProcW(h, msg, w, l);
    }

    switch (msg) {
        case WM_DISPLAYCHANGE:
            AT_LOG_INFO("WM_DISPLAYCHANGE → arm debounce");
            self->arm_debounce();
            if (self->displaychange_cb_) self->displaychange_cb_();
            return 0;
        case WM_TIMER:
            if (w == kDebounceTimerId) {
                self->debounce_timer_id_ = 0;
                AT_LOG_DEBUG("Debounce timer fired → re-tile");
                if (self->tile_cb_) self->tile_cb_();
            } else if (w == kStartupDelayTimerId) {
                if (self->startup_timer_id_) {
                    KillTimer(h, self->startup_timer_id_);
                    self->startup_timer_id_ = 0;
                }
                self->startup_delay_active_ = false;
                AT_LOG_INFO("Startup delay elapsed → catch-up tile");
                self->fire_tile_now();
            } else if (w == kPreviewTimerId) {
                KillTimer(h, kPreviewTimerId);
                if (self->preview_hide_cb_) self->preview_hide_cb_();
            }
            return 0;
        case WM_AT_TILE_NOW:
            self->fire_tile_now();
            return 0;
        case WM_AT_TOGGLE_PAUSE:
            self->fire_pause();
            return 0;
        case WM_AT_PREVIEW:
            if (self->cmd_cb_) self->cmd_cb_(Command::Preview);
            return 0;
        case WM_AT_RELOAD:
            if (self->reload_cb_) self->reload_cb_();
            return 0;
        case WM_AT_EXIT:
            PostQuitMessage(0);
            return 0;
        case WM_HOTKEY:
            self->handle_hotkey(static_cast<int>(w));
            return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

void CALLBACK EventSource::win_event_proc(HWINEVENTHOOK, DWORD, HWND, LONG,
                                          LONG, DWORD event, DWORD) {
    if (event != EVENT_GLOBAL_CREATE && event != EVENT_GLOBAL_DESTROY &&
        event != EVENT_GLOBAL_LOC) return;
    auto* self = instance_;
    if (!self || !self->hwnd_) return;
    if (self->startup_delay_active_) return;   // drop; delay timer fires a catch-up tile
    PostMessageW(self->hwnd_, WM_AT_TILE_NOW, 0, 0);
}

} // namespace autoterminal