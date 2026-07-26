#include "ui_bridge.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

#include "logger.h"

namespace autoterminal {

namespace {

constexpr UINT kHotkeyIdTile    = 1;
constexpr UINT kHotkeyIdPause   = 2;
constexpr UINT kHotkeyIdSettings = 3;

constexpr LPCWSTR kAutostartKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr LPCWSTR kAutostartName = L"AutoTerminal";

constexpr wchar_t kPopupHelperClass[] = L"AutoTerminal.PopupHelper.v1";

// Window used as BOTH:
//   1. a foreground-capable owner for shell-tray popups, AND
//   2. the callback target for Shell_NotifyIcon (replaces the previous
//      HWND_MESSAGE window that the Windows 11 shell silently ignores).
//
// Two reasons we cannot use a HWND_MESSAGE window here:
//
//   * TrackPopupMenu: a message-only window cannot become foreground, so
//     TrackPopupMenu silently fails (returns 0, no menu ever appears).
//
//   * Shell_NotifyIcon click routing: the Windows 11 shell delivers the
//     tray-icon click callback to a *real*, top-level window associated
//     with the registered icon. HWND_MESSAGE windows have no UI surface,
//     and the shell drops the click entirely — no callback is sent. We
//     observed this as "right-click does nothing, double-click does
//     nothing" in the live daemon.
//
// So we make this helper a *real*, top-level, visible (but off-screen and
// 1x1) window. The user never sees it, but Win32 treats it as a valid
// foreground candidate AND as a valid shell-notification callback target.
//
// WM_AT_TRAYICON (the tray-icon callback message) is delivered to this
// window and forwarded to UIBridge::on_tray_message() via the
// SetWindowLongPtrW GWLP_USERDATA pointer set at create time.
LRESULT CALLBACK popup_helper_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        if (cs) SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(h, m, w, l);
    }
    if (m == WM_AT_TRAYICON) {
        auto* self = reinterpret_cast<UIBridge*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (self) self->on_tray_message(h, l);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

constexpr int kPopupHelperSize = 1;                  // 1x1 px so the window is
                                                     // "real" but invisible
constexpr int kPopupHelperOffX = -32000;             // off-screen
constexpr int kPopupHelperOffY = -32000;

HWND create_popup_helper(HINSTANCE hinst, UIBridge* owner) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = popup_helper_proc;
        wc.hInstance     = hinst;
        wc.lpszClassName = kPopupHelperClass;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&wc)) {
            DWORD err = GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS) {
                AT_LOG_ERROR("PopupHelper RegisterClassEx failed err=%lu", err);
                return nullptr;
            }
        }
        registered = true;
    }
    // Real top-level window with WS_POPUP | WS_VISIBLE (no WS_DISABLED — that
    // blocks SetForegroundWindow on modern Windows). Positioned far off-screen
    // and 1x1 so the user never sees it but Win32 considers it a valid
    // foreground candidate AND a valid shell-notification callback target.
    HWND h = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kPopupHelperClass, L"",
        WS_POPUP | WS_VISIBLE,
        kPopupHelperOffX, kPopupHelperOffY, kPopupHelperSize, kPopupHelperSize,
        nullptr, nullptr, hinst, owner);
    if (!h) {
        AT_LOG_ERROR("CreateWindowEx for PopupHelper failed err=%lu", GetLastError());
    }
    return h;
}

} // namespace

void UIBridge::show_context_menu() {
    AT_LOG_INFO("Tray right-click: showing context menu");
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        AT_LOG_ERROR("CreatePopupMenu failed err=%lu", GetLastError());
        return;
    }
    AppendMenuW(menu, MF_STRING,                          TrayCmdTileNow,        L"Tile now");
    AppendMenuW(menu, MF_STRING,                          TrayCmdTogglePause,    L"Pause auto-tile");
    AppendMenuW(menu, MF_SEPARATOR,                       0,                     nullptr);
    UINT auto_flags = MF_STRING | (current_config_.autostart ? MF_CHECKED : 0);
    AppendMenuW(menu, auto_flags,                         TrayCmdToggleAutostart,L"Start with Windows");
    AppendMenuW(menu, MF_STRING,                          TrayCmdOpenConfig,     L"Open config file...");
    AppendMenuW(menu, MF_STRING,                          TrayCmdSettings,       L"Settings...");
    AppendMenuW(menu, MF_STRING,                          TrayCmdReload,         L"Reload config");
    AppendMenuW(menu, MF_SEPARATOR,                       0,                     nullptr);
    AppendMenuW(menu, MF_STRING,                          TrayCmdAbout,          L"About AutoTerminal");
    AppendMenuW(menu, MF_STRING,                          TrayCmdExit,           L"Exit AutoTerminal");

    POINT pt{};
    GetCursorPos(&pt);

    // The popup helper is a real, visible (but 1x1 and off-screen) top-level
    // window. A 0x0 invisible window cannot take foreground on Win10/11, so
    // TrackPopupMenu returns 0 with no menu ever displayed — this is the bug
    // we're working around.
    HWND owner = helper_hwnd_ ? helper_hwnd_ : hwnd_;
    if (!owner) {
        AT_LOG_ERROR("No owner window for tray popup");
        DestroyMenu(menu);
        return;
    }

    // Bring the helper back to a known state and make sure it's foreground.
    SetWindowPos(owner, nullptr,
                 kPopupHelperOffX, kPopupHelperOffY,
                 kPopupHelperSize, kPopupHelperSize,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(owner, SW_SHOWNOACTIVATE);

    // Attach to the current FG thread so SetForegroundWindow can bypass the
    // foreground-lock on Win10/11.
    HWND fg = GetForegroundWindow();
    DWORD fg_tid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD our_tid = GetCurrentThreadId();
    bool attached = false;
    if (fg_tid && fg_tid != our_tid) {
        AttachThreadInput(fg_tid, our_tid, TRUE);
        attached = true;
    }
    SetForegroundWindow(owner);
    if (attached) {
        AttachThreadInput(fg_tid, our_tid, FALSE);
    }

    int cmd = TrackPopupMenu(menu,
                             TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, owner, nullptr);
    ShowWindow(owner, SW_HIDE);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);

    AT_LOG_INFO("Tray menu picked id=%d", cmd);
    if (cmd == 0) return;
    dispatch(static_cast<TrayCommand>(cmd));
}

void UIBridge::dispatch(TrayCommand c) {
    if (cmd_cb_) cmd_cb_(c);
}

UIBridge::UIBridge() = default;
UIBridge::~UIBridge() { shutdown(); }

bool UIBridge::init(HWND hwnd) {
    hwnd_ = hwnd;
    helper_hwnd_ = create_popup_helper(GetModuleHandleW(nullptr), this);
    if (helper_hwnd_) {
        AT_LOG_INFO("Popup helper hwnd=0x%p (also tray callback target)", helper_hwnd_);
    } else {
        AT_LOG_WARN("Popup helper creation failed — tray clicks will not be delivered");
    }

    // Stable GUID so Windows persists this tray icon's visibility settings
    // across runs (without it, Win10/11 may shove a fresh-process icon into
    // the overflow ^(arrow) tray area, making it easy to miss).
    static const GUID kTrayIconGuid = {
        0x4f9db5e0, 0x3a21, 0x4e47,
        { 0xb5, 0xc9, 0xa8, 0xe2, 0xb1, 0xc0, 0xd1, 0x11 }
    };

    // Register the tray icon with the POPUP HELPER (a real, top-level window)
    // as the callback target — NOT with `hwnd`, which is the EventSource's
    // HWND_MESSAGE window. The Windows 11 shell does not deliver tray-icon
    // clicks to message-only windows (no UI surface), so we route callbacks
    // through the popup helper, whose wnd_proc forwards them to
    // UIBridge::on_tray_message via its GWLP_USERDATA pointer.
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = helper_hwnd_ ? helper_hwnd_ : hwnd;
    nid.uID              = 1;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP
                         | NIF_GUID | NIF_STATE;
    nid.uCallbackMessage = WM_AT_TRAYICON;
    nid.guidItem         = kTrayIconGuid;
    // Force the icon into the visible state, overriding any prior "hidden"
    // preference the user set.
    nid.dwState          = 0;
    nid.dwStateMask      = NIS_HIDDEN;
    nid.hIcon            = LoadIconW(nullptr, IDI_INFORMATION);
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"AutoTerminal — right-click for menu");
    added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (!added_) {
        AT_LOG_ERROR("Shell_NotifyIcon NIM_ADD failed err=%lu", GetLastError());
        return false;
    }
    // Enable NOTIFYICON_VERSION_4 so right-click / context-menu callbacks
    // fire reliably on modern Windows.
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    AT_LOG_INFO("Tray icon installed (callback hwnd=0x%p)", static_cast<void*>(nid.hWnd));
    return true;
}

void UIBridge::shutdown() {
    unregister_hotkeys();
    if (added_ && helper_hwnd_) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = helper_hwnd_;
        nid.uID    = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        added_ = false;
    }
    if (helper_hwnd_) {
        DestroyWindow(helper_hwnd_);
        helper_hwnd_ = nullptr;
    }
    hwnd_ = nullptr;
}

void UIBridge::register_hotkeys() {
    unregister_hotkeys();
    if (!hwnd_) return;
    if (RegisterHotKey(hwnd_, kHotkeyIdTile,  current_config_.hotkey_tile.modifiers,
                       current_config_.hotkey_tile.vk)) {
        std::wstring wide = format_hotkey(current_config_.hotkey_tile);
        std::string label(wide.size(), '\0');
        for (size_t i = 0; i < wide.size(); ++i) label[i] = static_cast<char>(wide[i] & 0x7F);
        AT_LOG_INFO("Registered hotkey tile: %s", label.c_str());
    } else {
        AT_LOG_WARN("Failed to register hotkey tile (mods=0x%X vk=0x%X) - is it already in use?",
                    current_config_.hotkey_tile.modifiers, current_config_.hotkey_tile.vk);
    }
    if (RegisterHotKey(hwnd_, kHotkeyIdPause, current_config_.hotkey_toggle_pause.modifiers,
                       current_config_.hotkey_toggle_pause.vk)) {
        std::wstring wide = format_hotkey(current_config_.hotkey_toggle_pause);
        std::string label(wide.size(), '\0');
        for (size_t i = 0; i < wide.size(); ++i) label[i] = static_cast<char>(wide[i] & 0x7F);
        AT_LOG_INFO("Registered hotkey pause: %s", label.c_str());
    } else {
        AT_LOG_WARN("Failed to register hotkey pause (mods=0x%X vk=0x%X) - is it already in use?",
                    current_config_.hotkey_toggle_pause.modifiers, current_config_.hotkey_toggle_pause.vk);
    }
}

void UIBridge::unregister_hotkeys() {
    if (!hwnd_) return;
    UnregisterHotKey(hwnd_, kHotkeyIdTile);
    UnregisterHotKey(hwnd_, kHotkeyIdPause);
}

void UIBridge::apply_config(const Config& cfg) {
    current_config_ = cfg;
    register_hotkeys();
}

void UIBridge::on_tray_message(HWND /*hwnd*/, LPARAM lparam) {
    UINT msg = static_cast<UINT>(lparam);
    AT_LOG_INFO("on_tray_message: lparam=0x%X (msg=%s)",
                (unsigned)msg,
                msg == WM_LBUTTONDOWN  ? "LBUTTONDOWN"  :
                msg == WM_LBUTTONUP    ? "LBUTTONUP"    :
                msg == WM_LBUTTONDBLCLK? "LBUTTONDBLCLK":
                msg == WM_RBUTTONDOWN  ? "RBUTTONDOWN"  :
                msg == WM_RBUTTONUP    ? "RBUTTONUP"    :
                msg == WM_RBUTTONDBLCLK? "RBUTTONDBLCLK":
                msg == WM_CONTEXTMENU  ? "CONTEXTMENU"  :
                msg == WM_MOUSEMOVE    ? "MOUSEMOVE"    : "OTHER");
    switch (msg) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
        case WM_RBUTTONDOWN:
            show_context_menu();
            break;
        case WM_LBUTTONDBLCLK:
            dispatch(TrayCmdTileNow);
            break;
        case WM_LBUTTONUP:
            // Some shells send only LBUTTONUP — show the menu anyway, treating
            // it as a single-click activation. Better to over-show than miss.
            show_context_menu();
            break;
    }
}

void UIBridge::open_config_file() {
    auto p = config_path();
    std::wstring s = L"notepad.exe \"";
    s += p.wstring();
    s += L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, s.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &pi)) {
        AT_LOG_WARN("Failed to open config file");
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

bool UIBridge::set_autostart(bool enable) {
    HKEY key{};
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kAutostartKey, 0,
                            KEY_SET_VALUE | KEY_QUERY_VALUE, &key);
    if (rc != ERROR_SUCCESS) {
        AT_LOG_WARN("RegOpenKeyEx failed for autostart");
        return false;
    }
    if (enable) {
        // --silent makes the binary stay in the tray at logon instead of
        // popping the settings window in front of the user.
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring cmd = L"\"";
        cmd += path;
        cmd += L"\" --silent";
        rc = RegSetValueExW(key, kAutostartName, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kAutostartName);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

} // namespace autoterminal
