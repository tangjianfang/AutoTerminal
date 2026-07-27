#include "settings_dialog.h"

#include "logger.h"
#include "monitor_index.h"
#include "ui_bridge.h"

#include <nfui/Application.hpp>
#include <nfui/Dpi.hpp>
#include <nfui/Controls/Button.hpp>
#include <nfui/Controls/CheckBox.hpp>
#include <nfui/Controls/ComboBox.hpp>
#include <nfui/Controls/Edit.hpp>
#include <nfui/Controls/StaticText.hpp>
#include <nfui/Font.hpp>
#include <nfui/Theme.hpp>
#include <nfui/Window.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace autoterminal {

namespace {

constexpr wchar_t kClass[]        = L"AutoTerminal.SettingsWindow.v1";
constexpr wchar_t kTitle[]        = L"AutoTerminal Settings";

enum CtrlId {
    IDC_MONITOR_LABEL    = 1001,
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

// --------------- small free-function helpers (unchanged) -------------------

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

// --------------- the NFUI-backed window subclass --------------------------

class SettingsWindow final : public nfui::Window {
public:
    SettingsWindow(HINSTANCE inst, Config initial, SettingsCallbacks cbs)
        : inst_(inst), cfg_(std::move(initial)), cbs_(std::move(cbs)),
          palette_(nfui::theme_palette(nfui::resolve_theme_mode(nfui::ThemeMode::system))) {}

    bool create_main(int show_cmd) {
        if (!create({inst_, kClass, kTitle,
                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                     WS_EX_DLGMODALFRAME,
                     CW_USEDEFAULT, CW_USEDEFAULT, 560, 380,
                     nullptr, nullptr})) {
            return false;
        }

        dpi_ = nfui::DpiScale(nfui::dpi_of(hwnd())).dpi();   // capture at create time

        int row_h = 28, gap = 8;
        int x = 16, label_w = 140, field_w = 360;
        int y = 16;

        // -- Display row ------------------------------------------------
        add_label(x, y, label_w, row_h, L"&Display", IDC_MONITOR_LABEL);
        add_combo(x + label_w + 8, y, field_w, IDC_MONITOR_COMBO);
        y += row_h + gap;

        // -- Processes ----------------------------------------------------
        add_label(x, y, label_w, row_h, L"&Processes (comma-separated)",
                  IDC_PROCESS_LABEL);
        add_edit(x + label_w + 8, y, field_w, row_h, IDC_PROCESS_EDIT);
        y += row_h + gap;

        // -- Padding ------------------------------------------------------
        add_label(x, y, label_w, row_h, L"&Padding (px)", IDC_PADDING_LABEL);
        add_edit(x + label_w + 8, y, 80, row_h, IDC_PADDING_EDIT);
        y += row_h + gap;

        // -- Hotkeys (display + capture) ---------------------------------
        int cap_w = 96, disp_w = field_w - cap_w - 6;
        add_label(x, y, label_w, row_h, L"&Tile-now hotkey", IDC_HK_TILE_LABEL);
        add_edit(x + label_w + 8, y, disp_w, row_h, IDC_HK_TILE_DISPLAY, true);
        add_button(x + label_w + 8 + disp_w + 6, y, cap_w, row_h,
                   L"Captur&e", IDC_HK_TILE_CAPTURE);
        y += row_h + gap;

        add_label(x, y, label_w, row_h, L"P&ause hotkey", IDC_HK_PAUSE_LABEL);
        add_edit(x + label_w + 8, y, disp_w, row_h, IDC_HK_PAUSE_DISPLAY, true);
        add_button(x + label_w + 8 + disp_w + 6, y, cap_w, row_h,
                   L"Capt&ure", IDC_HK_PAUSE_CAPTURE);
        y += row_h + gap;

        // -- Autostart ----------------------------------------------------
        add_check(x, y, field_w + label_w, row_h,
                  L"Start with &Windows (auto-launch at logon)",
                  IDC_AUTOSTART_CHECK);
        y += row_h + gap;

        // -- Log level ----------------------------------------------------
        add_label(x, y, label_w, row_h, L"&Log level", IDC_LOGLEVEL_LABEL);
        add_combo(x + label_w + 8, y, 160, IDC_LOGLEVEL_COMBO);
        y += row_h + gap + 8;

        // -- Buttons ------------------------------------------------------
        add_button(x, y, 150, row_h + 4, L"Open &config file...",
                   IDC_OPEN_CONFIG_BTN);
        int btn_w = 90, btn_gap = 8;
        int right0 = x + label_w + 8 + field_w;
        add_button(right0 - 2 * btn_w - btn_gap, y, btn_w, row_h + 4,
                   L"&Apply", IDC_APPLY_BTN);
        add_button(right0 - btn_w, y, btn_w, row_h + 4,
                   L"Cancel", IDC_CANCEL_BTN);
        // Exit button leftmost after Open config, prominent red-ish via
        // secondary styling (NFUI will draw with palette.danger accent).
        int exit_x = x + 158;
        nfui::ButtonStyle est{};
        est.secondary = true;
        exit_btn_.set_style(est);
        add_button(exit_x, y, 150, row_h + 4, L"E&xit AutoTerminal",
                   IDC_EXIT_BTN);

        // Initial state from the supplied Config.
        populate_monitors();
        populate_log_levels();
        apply_text_widgets();
        apply_check_state();
        refresh_hotkey_labels();

        ShowWindow(hwnd(), show_cmd);
        UpdateWindow(hwnd());
        AT_LOG_INFO("Settings window created hwnd=0x%p",
                    static_cast<void*>(hwnd()));
        return true;
    }

protected:
    // -------- message handling ----------------------------------------

    LRESULT handle_message(UINT m, WPARAM w, LPARAM l) override {
        switch (m) {
            case WM_KEYDOWN:   return on_keydown(w);
            case WM_CLOSE:     on_close();                  return 0;
            case WM_ERASEBKGND:
                // NFUI controls paint themselves; suppress erase to avoid
                // the brief background flicker before the first paint.
                return 1;
            case WM_CTLCOLORSTATIC: {
                HDC dc = reinterpret_cast<HDC>(w);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, palette_.text.rgb);
                return reinterpret_cast<LRESULT>(CreateSolidBrush(palette_.background.rgb));
            }
            case WM_DPICHANGED: {
                dpi_ = LOWORD(w);   // per-monitor DPI of the new monitor
                // Bump our cached NFUI fonts (next paint rebuilds them).
                // NFUI controls query DpiScale themselves for paint, so we
                // just need to invalidate everything.
                InvalidateRect(hwnd(), nullptr, TRUE);
                return 0;
            }
        }
        return nfui::Window::handle_message(m, w, l);
    }

    bool on_command(int id, HWND /*src*/, UINT code) override {
        switch (id) {
            case IDC_HK_TILE_CAPTURE:  if (code == BN_CLICKED) { start_capture(CapTile);  return true; } break;
            case IDC_HK_PAUSE_CAPTURE: if (code == BN_CLICKED) { start_capture(CapPause); return true; } break;
            case IDC_APPLY_BTN:        if (code == BN_CLICKED) { on_apply();   return true; } break;
            case IDC_CANCEL_BTN:       if (code == BN_CLICKED) { on_close();   return true; } break;
            case IDC_OPEN_CONFIG_BTN:  if (code == BN_CLICKED) { UIBridge::open_config_file(); return true; } break;
            case IDC_EXIT_BTN:         if (code == BN_CLICKED) { on_exit_btn(); return true; } break;
            default: break;
        }
        return false;
    }

private:
    // -------- control-builders (NFUI) ---------------------------------

    void add_label(int x, int y, int w, int h, std::wstring_view text, int id) {
        nfui::ControlCreateParams p{inst_, hwnd(), id, text, x, y, w, h};
        labels_.push_back(std::make_unique<nfui::StaticText>());
        (void)labels_.back()->inject_theme(&palette_, &fonts_);
        // Subtle body labels: sm size, left-aligned.
        nfui::TextStyle ts{};
        ts.font_size_pt = nfui::font_pt::sm;
        ts.align_v = nfui::StaticTextAlignV::middle;
        (void)labels_.back()->set_style(ts);
        (void)labels_.back()->create(p);
    }

    void add_edit(int x, int y, int w, int h, int id, bool readonly = false) {
        nfui::ControlCreateParams p{inst_, hwnd(), id, L"",
                                    x, y, w, h,
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    (readonly ? ES_READONLY : ES_AUTOHSCROLL)};
        if (id == IDC_PADDING_EDIT) p.style |= ES_NUMBER;
        p.ex_style = WS_EX_CLIENTEDGE;
        edits_.push_back(std::make_unique<nfui::Edit>());
        (void)edits_.back()->inject_theme(&palette_, &fonts_);
        (void)edits_.back()->create(p);
        // Edit is a native control — needs explicit font adoption.
        HFONT f = (id == IDC_PADDING_EDIT)
                    ? fonts_.mono(dpi_, nfui::font_pt::sm)
                    : fonts_.regular(dpi_, nfui::font_pt::sm);
        SendMessageW(edits_.back()->hwnd(), WM_SETFONT,
                     reinterpret_cast<WPARAM>(f), TRUE);
    }

    void add_button(int x, int y, int w, int h, std::wstring_view text, int id) {
        nfui::ControlCreateParams p{inst_, hwnd(), id, text, x, y, w, h};
        // Owner-draw NFUI Button paints itself; suppress native style bits.
        switch (id) {
            case IDC_APPLY_BTN:
            case IDC_OPEN_CONFIG_BTN:
            case IDC_HK_TILE_CAPTURE:
            case IDC_HK_PAUSE_CAPTURE: {
                // Primary accent face.
                nfui::Button& b = button_for(id);
                (void)b.inject_theme(&palette_, &fonts_);
                (void)b.create(p);
                break;
            }
            case IDC_CANCEL_BTN:
            case IDC_EXIT_BTN: {
                // Secondary (surface tone) face for the destructive / neutral
                // actions so they don't compete with Apply.
                nfui::Button& b = button_for(id);
                nfui::ButtonStyle s{};
                s.secondary = true;
                (void)b.set_style(s);
                (void)b.inject_theme(&palette_, &fonts_);
                (void)b.create(p);
                // IDC_EXIT_BTN routes through button_for() to exit_btn_ directly,
                // so no separate alias assignment is needed.
                break;
            }
        }
    }

    void add_check(int x, int y, int w, int h, std::wstring_view text, int id) {
        (void)id;
        nfui::ControlCreateParams p{inst_, hwnd(), id, text, x, y, w, h};
        (void)auto_check_.inject_theme(&palette_, &fonts_);
        (void)auto_check_.create(p);
    }

    void add_combo(int x, int y, int w, int id) {
        nfui::ControlCreateParams p{inst_, hwnd(), id, L"",
                                    x, y, w, 220,
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    CBS_DROPDOWNLIST};
        switch (id) {
            case IDC_MONITOR_COMBO:
                (void)monitor_combo_.inject_theme(&palette_, &fonts_);
                (void)monitor_combo_.create(p);
                break;
            case IDC_LOGLEVEL_COMBO:
                (void)loglevel_combo_.inject_theme(&palette_, &fonts_);
                (void)loglevel_combo_.create(p);
                break;
            default: break;
        }
        // NFUI ComboBox wraps the native control; set font.
        HWND h = GetDlgItem(hwnd(), id);
        HFONT f = fonts_.regular(dpi_, nfui::font_pt::sm);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
    }

    nfui::Button& button_for(int id) {
        switch (id) {
            case IDC_HK_TILE_CAPTURE:  return btn_tile_cap_;
            case IDC_HK_PAUSE_CAPTURE: return btn_pause_cap_;
            case IDC_APPLY_BTN:        return btn_apply_;
            case IDC_CANCEL_BTN:       return btn_cancel_;
            case IDC_OPEN_CONFIG_BTN:  return btn_open_cfg_;
            case IDC_EXIT_BTN:         return exit_btn_;
        }
        return btn_apply_;
    }

    // -------- state machine -------------------------------------------

    void populate_monitors() {
        HWND combo = GetDlgItem(hwnd(), IDC_MONITOR_COMBO);
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
            if (monitors[i].friendly_name == cfg_.target_monitor ||
                monitors[i].gdi_name == cfg_.target_monitor) {
                sel = pos;
            }
        }
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
    }

    void populate_log_levels() {
        HWND combo = GetDlgItem(hwnd(), IDC_LOGLEVEL_COMBO);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        struct L { LogLevel v; const wchar_t* name; };
        L ls[] = {
            { LogLevel::Debug, L"debug" },
            { LogLevel::Info,  L"info"  },
            { LogLevel::Warn,  L"warn"  },
            { LogLevel::Error, L"error" },
        };
        int sel = 1;
        for (int i = 0; i < 4; ++i) {
            int pos = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                                     reinterpret_cast<LPARAM>(ls[i].name)));
            SendMessageW(combo, CB_SETITEMDATA, pos, static_cast<LPARAM>(ls[i].v));
            if (ls[i].v == cfg_.log_level) sel = i;
        }
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
    }

    void apply_text_widgets() {
        SetWindowTextW(GetDlgItem(hwnd(), IDC_PROCESS_EDIT),
                       join_csv(cfg_.process_names).c_str());
        set_edit_int(GetDlgItem(hwnd(), IDC_PADDING_EDIT), cfg_.padding);
    }

    void apply_check_state() {
        auto_check_.set_checked(cfg_.autostart);
    }

    void refresh_hotkey_labels() {
        std::wstring t = format_hotkey(tile_hk());
        std::wstring p = format_hotkey(pause_hk());
        if (cap_ == CapTile)  t = L"Press a key combo (Esc to cancel)...";
        if (cap_ == CapPause) p = L"Press a key combo (Esc to cancel)...";
        SetWindowTextW(GetDlgItem(hwnd(), IDC_HK_TILE_DISPLAY),  t.c_str());
        SetWindowTextW(GetDlgItem(hwnd(), IDC_HK_PAUSE_DISPLAY), p.c_str());
    }

    void start_capture(CaptureState cs) {
        cap_ = cs;
        refresh_hotkey_labels();
        SetFocus(hwnd());
    }

    void cancel_capture() {
        cap_ = CapNone;
        pending_tile_.reset();
        pending_pause_.reset();
        refresh_hotkey_labels();
    }

    LRESULT on_keydown(WPARAM vk) {
        if (cap_ == CapNone) return 1;
        if (vk == VK_ESCAPE) { cancel_capture(); return 0; }
        if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_MENU  || vk == VK_LMENU  || vk == VK_RMENU ||
            vk == VK_LWIN  || vk == VK_RWIN) {
            return 0;
        }
        finish_capture(static_cast<UINT>(vk));
        return 0;
    }

    void finish_capture(UINT vk) {
        UINT mods = MOD_NOREPEAT;
        if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
        if (GetKeyState(VK_MENU)     & 0x8000) mods |= MOD_ALT;
        if (GetKeyState(VK_SHIFT)    & 0x8000) mods |= MOD_SHIFT;
        if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= MOD_WIN;
        Hotkey hk{mods, vk};
        CaptureState finished = cap_;
        if (cap_ == CapTile)  pending_tile_  = hk;
        if (cap_ == CapPause) pending_pause_ = hk;
        cap_ = CapNone;
        refresh_hotkey_labels();
        HWND btn = GetDlgItem(hwnd(),
            finished == CapTile ? IDC_HK_TILE_CAPTURE : IDC_HK_PAUSE_CAPTURE);
        if (btn) SetFocus(btn);
    }

    void on_apply() {
        std::wstring txt;
        if (get_edit_text(GetDlgItem(hwnd(), IDC_PROCESS_EDIT), txt)) {
            cfg_.process_names = split_csv(txt);
        }
        int pad_v = 0;
        if (get_edit_int(GetDlgItem(hwnd(), IDC_PADDING_EDIT), pad_v)) {
            cfg_.padding = std::max(0, pad_v);
        }
        HWND cb = GetDlgItem(hwnd(), IDC_MONITOR_COMBO);
        int sel = static_cast<int>(SendMessageW(cb, CB_GETCURSEL, 0, 0));
        int data = static_cast<int>(SendMessageW(cb, CB_GETITEMDATA, sel, 0));
        if (sel <= 0 || data == 0) {
            cfg_.target_monitor.clear();
        } else {
            auto monitors = enumerate_monitors();
            size_t mi = static_cast<size_t>(data - 1);
            if (mi < monitors.size()) cfg_.target_monitor = monitors[mi].friendly_name;
        }
        HWND ll = GetDlgItem(hwnd(), IDC_LOGLEVEL_COMBO);
        int lsel = static_cast<int>(SendMessageW(ll, CB_GETCURSEL, 0, 0));
        int ldata = static_cast<int>(SendMessageW(ll, CB_GETITEMDATA, lsel, 0));
        cfg_.log_level = static_cast<LogLevel>(ldata);
        cfg_.autostart = auto_check_.checked();
        if (pending_tile_)  cfg_.hotkey_tile        = *pending_tile_;
        if (pending_pause_) cfg_.hotkey_toggle_pause = *pending_pause_;
        pending_tile_.reset();
        pending_pause_.reset();
        refresh_hotkey_labels();
        if (cbs_.on_apply) cbs_.on_apply(cfg_);
        ShowWindow(hwnd(), SW_HIDE);
    }

    void on_close() {
        cancel_capture();
        ShowWindow(hwnd(), SW_HIDE);
        if (cbs_.on_close) cbs_.on_close();
    }

    void on_exit_btn() {
        int rc = MessageBoxW(hwnd(),
            L"Exit AutoTerminal?\n\n"
            L"This stops the background daemon. Tiling will no longer "
            L"happen until you relaunch AutoTerminal.",
            L"Exit AutoTerminal",
            MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
        if (rc == IDYES && cbs_.on_exit) cbs_.on_exit();
    }

    Hotkey tile_hk()  const { return pending_tile_  ? *pending_tile_  : cfg_.hotkey_tile; }
    Hotkey pause_hk() const { return pending_pause_ ? *pending_pause_ : cfg_.hotkey_toggle_pause; }

    // -------- members -------------------------------------------------

    HINSTANCE inst_{};
    Config     cfg_{};
    SettingsCallbacks cbs_{};
    int        dpi_{96};
    CaptureState cap_{CapNone};
    std::optional<Hotkey> pending_tile_;
    std::optional<Hotkey> pending_pause_;

    nfui::ThemePalette palette_{};
    nfui::FontCache    fonts_{};

    // Owned NFUI controls. The labels / edits vectors avoid enumerating
    // 6 distinct member variables for the row labels; the buttons that
    // need individually-accessed styling keep named members.
    // nfui::Control (and derived) deletes both copy and move, so they cannot
    // live in a std::vector<...> directly. We heap-allocate via unique_ptr
    // and own them through SettingsWindow's lifetime.
    std::vector<std::unique_ptr<nfui::StaticText>> labels_;
    std::vector<std::unique_ptr<nfui::Edit>>       edits_;
    nfui::Button                  btn_tile_cap_{};
    nfui::Button                  btn_pause_cap_{};
    nfui::Button                  btn_apply_{};
    nfui::Button                  btn_cancel_{};
    nfui::Button                  btn_open_cfg_{};
    nfui::Button                  exit_btn_{};
    nfui::CheckBox                auto_check_{};
    nfui::ComboBox                monitor_combo_{};
    nfui::ComboBox                loglevel_combo_{};
};

} // namespace

HWND create_settings_window(HINSTANCE hinst, Config initial, SettingsCallbacks cbs) {
    auto* w = new SettingsWindow(hinst, std::move(initial), std::move(cbs));
    if (!w->create_main(SW_HIDE)) {
        delete w;
        return nullptr;
    }
    return w->hwnd();
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
    // The window processes WM_KEYDOWN directly via the NFUI Window hook;
    // this external entry point is retained for ABI compatibility.
    return false;
}

} // namespace autoterminal