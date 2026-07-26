#include "settings_dialog.h"

#include "logger.h"
#include "monitor_index.h"
#include "ui_bridge.h"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace autoterminal {

namespace {

constexpr wchar_t kClass[] = L"AutoTerminal.SettingsWindow.v1";
constexpr wchar_t kTitle[] = L"AutoTerminal Settings";

enum CtrlId : WORD {
    IDC_MONITOR_LABEL = 1001,
    IDC_MONITOR_COMBO,
    IDC_PROCESS_LABEL,
    IDC_PROCESS_EDIT,
    IDC_PADDING_LABEL,
    IDC_PADDING_EDIT,
    IDC_HK_TILE_LABEL,
    IDC_HK_TILE_DISPLAY,
    IDC_HK_TILE_CAPTURE,
    IDC_HK_PAUSE_LABEL,
    IDC_HK_PAUSE_DISPLAY,
    IDC_HK_PAUSE_CAPTURE,
    IDC_AUTOSTART_CHECK,
    IDC_LOGLEVEL_LABEL,
    IDC_LOGLEVEL_COMBO,
    IDC_OPEN_CONFIG_BTN,
    IDC_APPLY_BTN,
    IDC_CANCEL_BTN,
    IDC_EXIT_BTN,
};

enum CaptureState { CapNone, CapTile, CapPause };

HFONT get_ui_font() {
    static HFONT font = nullptr;
    if (font) return font;
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        font = CreateFontIndirectW(&ncm.lfMessageFont);
    } else {
        font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    return font;
}

HWND mk_label(HWND parent, WORD id, LPCWSTR text, int x, int y, int w, int hh) {
    HWND ctl = CreateWindowExW(0, WC_STATIC, text, WS_CHILD | WS_VISIBLE,
                                x, y, w, hh, parent,
                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
                                nullptr, nullptr);
    SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(get_ui_font()), TRUE);
    return ctl;
}

HWND mk_edit(HWND parent, WORD id, bool readonly, bool number_only,
             int x, int y, int w, int hh) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
    if (readonly)    style |= ES_READONLY;
    if (number_only) style |= ES_NUMBER;
    HWND ctl = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDIT, L"", style, x, y, w, hh, parent,
                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
                                nullptr, nullptr);
    SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(get_ui_font()), TRUE);
    return ctl;
}

HWND mk_btn(HWND parent, WORD id, LPCWSTR text, int x, int y, int w, int hh) {
    HWND ctl = CreateWindowExW(0, WC_BUTTON, text,
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                x, y, w, hh, parent,
                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
                                nullptr, nullptr);
    SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(get_ui_font()), TRUE);
    return ctl;
}

HWND mk_check(HWND parent, WORD id, LPCWSTR text, int x, int y, int w, int hh) {
    HWND ctl = CreateWindowExW(0, WC_BUTTON, text,
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                x, y, w, hh, parent,
                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
                                nullptr, nullptr);
    SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(get_ui_font()), TRUE);
    return ctl;
}

HWND mk_combo(HWND parent, WORD id, int x, int y, int w, int hh) {
    HWND ctl = CreateWindowExW(0, WC_COMBOBOX, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                x, y, w, hh, parent,
                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
                                nullptr, nullptr);
    SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(get_ui_font()), TRUE);
    return ctl;
}

void set_edit_int(HWND edit, int value) {
    wchar_t buf[32];
    swprintf_s(buf, L"%d", value);
    SetWindowTextW(edit, buf);
}

bool get_edit_int(HWND edit, int& out) {
    wchar_t buf[32];
    if (GetWindowTextW(edit, buf, 32) == 0) return false;
    int v = 0;
    if (swscanf_s(buf, L"%d", &v) != 1) return false;
    out = v;
    return true;
}

bool get_edit_text(HWND edit, std::wstring& out) {
    int len = GetWindowTextLengthW(edit);
    if (len < 0) return false;
    out.resize(static_cast<size_t>(len));
    if (len > 0) GetWindowTextW(edit, out.data(), len + 1);
    return true;
}

std::vector<std::wstring> split_csv(const std::wstring& s) {
    std::vector<std::wstring> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == L' ' || s[i] == L',' || s[i] == L'\t')) ++i;
        size_t start = i;
        while (i < s.size() && s[i] != L',') ++i;
        if (i > start) {
            std::wstring t = s.substr(start, i - start);
            while (!t.empty() && t.back() == L' ') t.pop_back();
            if (!t.empty()) out.push_back(std::move(t));
        }
    }
    if (out.empty()) out.push_back(L"WindowsTerminal.exe");
    return out;
}

std::wstring join_csv(const std::vector<std::wstring>& v) {
    std::wstring s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += L", ";
        s += v[i];
    }
    return s;
}

struct DialogState {
    Config  cfg;
    SettingsCallbacks cbs;
    HWND    hwnd = nullptr;
    HINSTANCE hi = nullptr;
    CaptureState cap = CapNone;
    std::optional<Hotkey> pending_tile;
    std::optional<Hotkey> pending_pause;
    CaptureState last_finished_cap = CapNone; // remembers what we just captured

    static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l);
    void on_create(HWND h, HINSTANCE hinst);
    void on_command(WORD id, HWND ctrl);
    LRESULT on_keydown(WPARAM vk);
    void on_close();
    void on_apply();

    void populate_monitors();
    void populate_log_levels();
    void refresh_hotkey_labels();
    void apply_text_widgets();
    void start_capture(CaptureState cs);
    void cancel_capture();
    void finish_capture(WPARAM vk);
    Hotkey tile_hk()  const { return pending_tile  ? *pending_tile  : cfg.hotkey_tile; }
    Hotkey pause_hk() const { return pending_pause ? *pending_pause : cfg.hotkey_toggle_pause; }
};

LRESULT CALLBACK DialogState::wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(l);
        auto* self = reinterpret_cast<DialogState*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->on_create(h, cs->hInstance);
        return 0;
    }
    auto* self = reinterpret_cast<DialogState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, m, w, l);
    switch (m) {
        case WM_COMMAND:   self->on_command(LOWORD(w), reinterpret_cast<HWND>(l)); return 0;
        case WM_KEYDOWN:   return self->on_keydown(w);
        case WM_CLOSE:     self->on_close();  return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(w);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
    }
    return DefWindowProcW(h, m, w, l);
}

void DialogState::on_create(HWND h, HINSTANCE hinst) {
    hwnd = h;
    hi = hinst;

    int row_h = 26, gap = 8;
    int x = 14, label_w_px = 130, field_w = 320;
    int y = 12;

    mk_label(h, IDC_MONITOR_LABEL,   L"&Display",        x,             y, label_w_px, row_h);
    mk_combo(h, IDC_MONITOR_COMBO,                     x + label_w_px + 8, y, field_w, 220);
    y += row_h + gap;

    mk_label(h, IDC_PROCESS_LABEL,   L"&Processes (comma-separated)", x, y, label_w_px, row_h);
    mk_edit (h, IDC_PROCESS_EDIT,    false, false,        x + label_w_px + 8, y, field_w, row_h);
    y += row_h + gap;

    mk_label(h, IDC_PADDING_LABEL,   L"&Padding (px)",   x,             y, label_w_px, row_h);
    mk_edit (h, IDC_PADDING_EDIT,    false, true,         x + label_w_px + 8, y, 70, row_h);
    y += row_h + gap;

    int cap_w = 100, disp_w = field_w - cap_w - 6;
    mk_label(h, IDC_HK_TILE_LABEL,   L"&Tile-now hotkey",x,             y, label_w_px, row_h);
    mk_edit (h, IDC_HK_TILE_DISPLAY, true,  false,        x + label_w_px + 8, y, disp_w, row_h);
    mk_btn  (h, IDC_HK_TILE_CAPTURE, L"&Capture",        x + label_w_px + 8 + disp_w + 6, y, cap_w, row_h);
    y += row_h + gap;

    mk_label(h, IDC_HK_PAUSE_LABEL,  L"P&ause hotkey",   x,             y, label_w_px, row_h);
    mk_edit (h, IDC_HK_PAUSE_DISPLAY,true,  false,        x + label_w_px + 8, y, disp_w, row_h);
    mk_btn  (h, IDC_HK_PAUSE_CAPTURE, L"C&apture",       x + label_w_px + 8 + disp_w + 6, y, cap_w, row_h);
    y += row_h + gap;

    HWND ac = mk_check(h, IDC_AUTOSTART_CHECK, L"Start with &Windows (auto-launch at logon)",
                       x, y, field_w + label_w_px, row_h);
    SendMessageW(ac, BM_SETCHECK, cfg.autostart ? BST_CHECKED : BST_UNCHECKED, 0);
    y += row_h + gap;

    mk_label(h, IDC_LOGLEVEL_LABEL,  L"&Log level",      x,             y, label_w_px, row_h);
    mk_combo(h, IDC_LOGLEVEL_COMBO,                     x + label_w_px + 8, y, 140, 220);
    y += row_h + gap + 10;

    int btn_w_ = 90, btn_gap = 8;
    int total_btns_w = 140 + 12 + btn_w_ + btn_gap + btn_w_;
    int right0 = x + label_w_px + 8 + field_w;
    mk_btn(h, IDC_OPEN_CONFIG_BTN, L"Open &config file...",
           x, y, 140, row_h);
    mk_btn(h, IDC_APPLY_BTN,       L"&Apply",
           right0 - 2 * btn_w_ - btn_gap, y, btn_w_, row_h);
    mk_btn(h, IDC_CANCEL_BTN,      L"Cancel",
           right0 - btn_w_, y, btn_w_, row_h);
    // Exit button — leftmost, red-ish label so it stands out. Fallback for
    // when the tray context menu is unavailable (icon hidden in overflow,
    // shell notification dropped, etc.).
    HWND exit_btn = mk_btn(h, IDC_EXIT_BTN, L"E&xit AutoTerminal",
                            x + 150, y, 140, row_h);
    (void)exit_btn;
    (void)btn_w_; (void)total_btns_w;

    populate_monitors();
    populate_log_levels();
    apply_text_widgets();
    refresh_hotkey_labels();

    // Initial size: width 500, height based on final y.
    RECT rc{0, 0, 500, y + 50};
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
    SetWindowPos(h, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                  SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void DialogState::populate_monitors() {
    HWND combo = GetDlgItem(hwnd, IDC_MONITOR_COMBO);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int primary_pos = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(L"(primary monitor - default)")));
    SendMessageW(combo, CB_SETITEMDATA, primary_pos, 0);
    auto monitors = enumerate_monitors();
    int sel = primary_pos;
    for (size_t i = 0; i < monitors.size(); ++i) {
        const auto& m = monitors[i];
        std::wstring label = m.friendly_name;
        if (m.primary) label += L"  (primary)";
        label += L"  -  ";
        label += m.gdi_name;
        int pos = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                                 reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(combo, CB_SETITEMDATA, pos, static_cast<LPARAM>(i + 1));
        if (monitors[i].friendly_name == cfg.target_monitor ||
            monitors[i].gdi_name == cfg.target_monitor) {
            sel = pos;
        }
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
}

void DialogState::populate_log_levels() {
    HWND combo = GetDlgItem(hwnd, IDC_LOGLEVEL_COMBO);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    struct L { LogLevel v; const wchar_t* name; };
    L ls[] = {
        { LogLevel::Debug, L"debug" },
        { LogLevel::Info,  L"info" },
        { LogLevel::Warn,  L"warn" },
        { LogLevel::Error, L"error" },
    };
    int sel = 1;
    for (int i = 0; i < 4; ++i) {
        int pos = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                                 reinterpret_cast<LPARAM>(ls[i].name)));
        SendMessageW(combo, CB_SETITEMDATA, pos, static_cast<LPARAM>(ls[i].v));
        if (ls[i].v == cfg.log_level) sel = i;
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
}

void DialogState::refresh_hotkey_labels() {
    std::wstring t  = format_hotkey(tile_hk());
    std::wstring p  = format_hotkey(pause_hk());
    if (cap == CapTile)  t = L"Press a key combo (Esc to cancel)...";
    if (cap == CapPause) p = L"Press a key combo (Esc to cancel)...";
    SetWindowTextW(GetDlgItem(hwnd, IDC_HK_TILE_DISPLAY),  t.c_str());
    SetWindowTextW(GetDlgItem(hwnd, IDC_HK_PAUSE_DISPLAY), p.c_str());
}

void DialogState::apply_text_widgets() {
    SetWindowTextW(GetDlgItem(hwnd, IDC_PROCESS_EDIT), join_csv(cfg.process_names).c_str());
    set_edit_int(GetDlgItem(hwnd, IDC_PADDING_EDIT), cfg.padding);
}

void DialogState::on_command(WORD id, HWND /*ctrl*/) {
    switch (id) {
        case IDC_HK_TILE_CAPTURE:  start_capture(CapTile);  return;
        case IDC_HK_PAUSE_CAPTURE: start_capture(CapPause); return;
        case IDC_APPLY_BTN:        on_apply();   return;
        case IDC_CANCEL_BTN:       on_close();   return;
        case IDC_OPEN_CONFIG_BTN:  UIBridge::open_config_file(); return;
        case IDC_EXIT_BTN: {
            // Hide the dialog first so the user sees the desktop, then ask
            // for confirmation — exiting kills the daemon with no undo.
            AT_LOG_INFO("Settings: user clicked Exit AutoTerminal");
            int rc = MessageBoxW(hwnd,
                L"Exit AutoTerminal?\n\n"
                L"This stops the background daemon. Tiling will no longer "
                L"happen until you relaunch AutoTerminal.",
                L"Exit AutoTerminal",
                MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
            if (rc == IDYES && cbs.on_exit) cbs.on_exit();
            return;
        }
        default: return;
    }
}

void DialogState::start_capture(CaptureState cs) {
    cap = cs;
    refresh_hotkey_labels();
    SetFocus(hwnd);
    SetActiveWindow(hwnd);
}

void DialogState::cancel_capture() {
    cap = CapNone;
    pending_tile.reset();
    pending_pause.reset();
    refresh_hotkey_labels();
}

LRESULT DialogState::on_keydown(WPARAM vk) {
    if (cap == CapNone) return 1;
    if (vk == VK_ESCAPE) { cancel_capture(); return 0; }
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
        vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
        vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
        vk == VK_LWIN || vk == VK_RWIN) {
        return 0;
    }
    finish_capture(vk);
    return 0;
}

void DialogState::finish_capture(WPARAM vk) {
    UINT mods = MOD_NOREPEAT;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
    if (GetKeyState(VK_MENU)     & 0x8000) mods |= MOD_ALT;
    if (GetKeyState(VK_SHIFT)    & 0x8000) mods |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= MOD_WIN;
    Hotkey hk{mods, static_cast<UINT>(vk)};
    last_finished_cap = cap;
    if (cap == CapTile)  pending_tile  = hk;
    if (cap == CapPause) pending_pause = hk;
    cap = CapNone;
    refresh_hotkey_labels();
    HWND btn = GetDlgItem(hwnd, last_finished_cap == CapTile
                                   ? IDC_HK_TILE_CAPTURE
                                   : IDC_HK_PAUSE_CAPTURE);
    if (btn) SetFocus(btn);
}

void DialogState::on_apply() {
    std::wstring txt;
    if (get_edit_text(GetDlgItem(hwnd, IDC_PROCESS_EDIT), txt)) {
        cfg.process_names = split_csv(txt);
    }
    int pad_v = 0;
    if (get_edit_int(GetDlgItem(hwnd, IDC_PADDING_EDIT), pad_v)) {
        cfg.padding = std::max(0, pad_v);
    }
    HWND cb = GetDlgItem(hwnd, IDC_MONITOR_COMBO);
    int sel = static_cast<int>(SendMessageW(cb, CB_GETCURSEL, 0, 0));
    int data = static_cast<int>(SendMessageW(cb, CB_GETITEMDATA, sel, 0));
    if (sel <= 0 || data == 0) {
        cfg.target_monitor.clear();
    } else {
        auto monitors = enumerate_monitors();
        size_t mi = static_cast<size_t>(data - 1);
        if (mi < monitors.size()) cfg.target_monitor = monitors[mi].friendly_name;
    }
    HWND ll = GetDlgItem(hwnd, IDC_LOGLEVEL_COMBO);
    int lsel = static_cast<int>(SendMessageW(ll, CB_GETCURSEL, 0, 0));
    int ldata = static_cast<int>(SendMessageW(ll, CB_GETITEMDATA, lsel, 0));
    cfg.log_level = static_cast<LogLevel>(ldata);
    HWND ac = GetDlgItem(hwnd, IDC_AUTOSTART_CHECK);
    cfg.autostart = (SendMessageW(ac, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (pending_tile)  cfg.hotkey_tile        = *pending_tile;
    if (pending_pause) cfg.hotkey_toggle_pause = *pending_pause;
    pending_tile.reset();
    pending_pause.reset();
    refresh_hotkey_labels();
    if (cbs.on_apply) cbs.on_apply(cfg);
    ShowWindow(hwnd, SW_HIDE);
}

void DialogState::on_close() {
    cancel_capture();
    ShowWindow(hwnd, SW_HIDE);
    if (cbs.on_close) cbs.on_close();
}

} // namespace

HWND create_settings_window(HINSTANCE hinst, Config initial, SettingsCallbacks cbs) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DialogState::wnd_proc;
        wc.hInstance     = hinst;
        wc.lpszClassName = kClass;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&wc)) {
            DWORD err = GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS) {
                AT_LOG_ERROR("SettingsDialog RegisterClassEx failed err=%lu", err);
                return nullptr;
            }
        }
        registered = true;
    }
    auto* state = new DialogState();
    state->cfg = std::move(initial);
    state->cbs = std::move(cbs);
    HWND win = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClass, kTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 320,
        nullptr, nullptr, hinst, state);
    if (!win) {
        delete state;
        return nullptr;
    }
    return win;
}

void show_settings_window(HWND hwnd, bool show) {
    if (!hwnd) return;
    ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE);
}

bool settings_window_visible(HWND hwnd) {
    if (!hwnd) return false;
    return IsWindowVisible(hwnd) != FALSE;
}

bool try_settings_capture_key(HWND, WPARAM) {
    // Hotkey capture is handled inline via the dialog's WM_KEYDOWN;
    // this hook is reserved for future global hotkey-substitution needs.
    return false;
}

} // namespace autoterminal
