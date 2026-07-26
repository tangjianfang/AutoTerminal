#include "ui_bridge.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

#include "logger.h"

namespace autoterminal {

namespace {

constexpr UINT kHotkeyIdTile  = 1;
constexpr UINT kHotkeyIdPause = 2;

constexpr LPCWSTR kAutostartKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr LPCWSTR kAutostartName = L"AutoTerminal";

// Format command line for autostart: "<exe>" ...quoted properly...
std::wstring quoted_exe_path() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s = L"\"";
    s += path;
    s += L"\"";
    return s;
}

void show_context_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, TrayCmdTileNow,        L"Tile now");
    AppendMenuW(menu, MF_STRING, TrayCmdTogglePause,    L"Pause auto-tile");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_CHECKED * 0, TrayCmdToggleAutostart,
                L"Start with Windows");
    AppendMenuW(menu, MF_STRING, TrayCmdOpenConfig,     L"Open config file...");
    AppendMenuW(menu, MF_STRING, TrayCmdReload,         L"Reload config");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayCmdAbout,          L"About");
    AppendMenuW(menu, MF_STRING, TrayCmdExit,           L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(menu,
                             TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == 0) return;
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
}

} // namespace

UIBridge::UIBridge() = default;
UIBridge::~UIBridge() { shutdown(); }

bool UIBridge::init(HWND hwnd) {
    hwnd_ = hwnd;

    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd;
    nid.uID              = 1;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_AT_TRAYICON;
    nid.hIcon            = LoadIconW(nullptr, IDI_APPLICATION);
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_WINLOGO);
    wcscpy_s(nid.szTip, L"AutoTerminal");
    added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (!added_) {
        AT_LOG_ERROR("Shell_NotifyIcon NIM_ADD failed");
        return false;
    }
    return true;
}

void UIBridge::shutdown() {
    unregister_hotkeys();
    if (added_ && hwnd_) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = hwnd_;
        nid.uID    = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        added_ = false;
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
        AT_LOG_WARN("Failed to register hotkey tile");
    }
    if (RegisterHotKey(hwnd_, kHotkeyIdPause, current_config_.hotkey_toggle_pause.modifiers,
                       current_config_.hotkey_toggle_pause.vk)) {
        std::wstring wide = format_hotkey(current_config_.hotkey_toggle_pause);
        std::string label(wide.size(), '\0');
        for (size_t i = 0; i < wide.size(); ++i) label[i] = static_cast<char>(wide[i] & 0x7F);
        AT_LOG_INFO("Registered hotkey pause: %s", label.c_str());
    } else {
        AT_LOG_WARN("Failed to register hotkey pause");
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

void UIBridge::on_tray_message(HWND hwnd, LPARAM lparam) {
    UINT msg = static_cast<UINT>(lparam);
    switch (msg) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            show_context_menu(hwnd);
            break;
        case WM_LBUTTONDBLCLK:
            if (cmd_cb_) cmd_cb_(TrayCmdTileNow);
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
        std::wstring cmd = quoted_exe_path();
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