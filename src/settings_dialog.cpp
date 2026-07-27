#include "settings_dialog.h"

#include "logger.h"
#include "monitor_index.h"
#include "process_lister.h"
#include "ui_bridge.h"

#include <nfui/Application.hpp>
#include <nfui/Dpi.hpp>
#include <nfui/Controls/Button.hpp>
#include <nfui/Controls/CheckBox.hpp>
#include <nfui/Controls/ComboBox.hpp>
#include <nfui/Controls/Edit.hpp>
#include <nfui/Controls/ListBox.hpp>
#include <nfui/Controls/StaticText.hpp>
#include <nfui/Font.hpp>
#include <nfui/Theme.hpp>
#include <nfui/Window.hpp>

#include <commdlg.h>      // GetOpenFileNameW / GetSaveFileNameW (excluded by WIN32_LEAN_AND_MEAN)
#include <commctrl.h>     // SetWindowSubclass / DefSubclassProc (listbox drag reorder)
#include <filesystem>     // std::filesystem::copy_file / path
#include <system_error>   // std::error_code

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace autoterminal {

namespace {

constexpr wchar_t kClass[] = L"AutoTerminal.SettingsWindow.v1";
constexpr wchar_t kTitle[] = L"AutoTerminal Settings";

// Logical-pixel layout grid (100 % DPI baseline). All coordinates flow through
// DpiScale::logical_to_pixels at build time so the dialog stays aligned at any
// DPI. Heights here are at 100 % DPI; bump kDefaultHeightPx if you add rows.
constexpr int kDefaultWidthPx  = 580;
constexpr int kDefaultHeightPx = 500;   // +2 rows: Config file + Filter running
constexpr int kRowH            = 24;
constexpr int kGap             = 6;
constexpr int kGapTight        = 4;
constexpr int kGapBeforeButton = 8;
constexpr int kLeftMargin      = 14;
constexpr int kTopMargin       = 12;
constexpr int kBottomMargin    = 14;
constexpr int kLabelW          = 168;
constexpr int kFieldW          = 360;
constexpr int kPaddingFieldW   = 80;
constexpr int kLogLevelComboW  = 160;
constexpr int kCaptureBtnW     = 96;
constexpr int kAddBtnW         = 90;
constexpr int kBtnGap          = 8;
constexpr int kOpenCfgBtnW     = 150;
constexpr int kExitBtnW        = 150;
constexpr int kRefreshBtnW     = 80;
constexpr int kExportBtnW      = 100;
constexpr int kImportBtnW      = 100;
constexpr int kProcListH       = 72;
constexpr int kComboDropHeight = 220;

enum CtrlId {
    IDC_MONITOR_LABEL = 1001,
    IDC_MONITOR_COMBO,

    IDC_PROCESS_LABEL,            // column label "Processes"
    IDC_PROC_NAME_EDIT,           // row A: free-text input
    IDC_PROC_NAME_ADD,            // row A: Add
    IDC_PROC_PICK_COMBO,          // row B: running-process picker
    IDC_PROC_PICK_REFRESH,        // row B: refresh running-process list
    IDC_PROC_PICK_ADD,            // row B: add picked
    IDC_PROC_LIST,                // row C: configured processes listbox
    IDC_PROC_REMOVE,              // row C: remove selected from listbox

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

    IDC_CFGFILE_LABEL,           // "Config file" row label
    IDC_EXPORT_CONFIG_BTN,       // export config.toml to a chosen path
    IDC_IMPORT_CONFIG_BTN,       // load a chosen config.toml into the dialog

    IDC_PROC_FILTER_LABEL,       // "Filter running" label
    IDC_PROC_FILTER_EDIT,        // live-filter the running-process picker
};

enum CaptureState { CapNone, CapTile, CapPause };

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

std::wstring trim_ws(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t')) --b;
    return s.substr(a, b - a);
}

// ASCII-only lowercase (A-Z -> a-z). Process basenames are ASCII filenames,
// so this avoids towlower's locale surprises (e.g. Turkish dotless i).
std::wstring ascii_lower(std::wstring s) {
    for (auto& c : s) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return s;
}

// Case-insensitive substring test for the process-name filter. An empty
// needle matches everything.
bool contains_ci(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    return ascii_lower(haystack).find(ascii_lower(needle)) != std::wstring::npos;
}

bool listbox_contains(HWND lb, const std::wstring& s) {
    int n = static_cast<int>(SendMessageW(lb, LB_GETCOUNT, 0, 0));
    for (int i = 0; i < n; ++i) {
        wchar_t buf[256]{};
        SendMessageW(lb, LB_GETTEXT, i, reinterpret_cast<LPARAM>(buf));
        if (s == buf) return true;
    }
    return false;
}

// Common-dialog file pickers for Export/Import. Both open the browse box at
// the config directory and pin the TOML filter; return false on cancel.
bool pick_save_path(HWND owner, const std::wstring& initial_dir,
                    std::wstring& out_path) {
    wchar_t buf[MAX_PATH] = L"config.toml";
    OPENFILENAMEW ofn{};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = owner;
    ofn.lpstrFilter     = L"TOML config (*.toml)\0*.toml\0All files (*.*)\0*.*\0";
    ofn.nFilterIndex    = 1;
    ofn.lpstrFile       = buf;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrInitialDir = initial_dir.c_str();
    ofn.lpstrTitle      = L"Export AutoTerminal config";
    ofn.lpstrDefExt     = L"toml";
    ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return false;
    out_path = buf;
    return true;
}

bool pick_open_path(HWND owner, const std::wstring& initial_dir,
                    std::wstring& out_path) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = owner;
    ofn.lpstrFilter     = L"TOML config (*.toml)\0*.toml\0All files (*.*)\0*.*\0";
    ofn.nFilterIndex    = 1;
    ofn.lpstrFile       = buf;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrInitialDir = initial_dir.c_str();
    ofn.lpstrTitle      = L"Import AutoTerminal config";
    ofn.lpstrDefExt     = L"toml";
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    out_path = buf;
    return true;
}

// --------------- the NFUI-backed window subclass --------------------------

class SettingsWindow final : public nfui::Window {
public:
    SettingsWindow(HINSTANCE inst, Config initial, SettingsCallbacks cbs)
        : inst_(inst), cfg_(std::move(initial)), cbs_(std::move(cbs)),
          palette_(nfui::theme_palette(nfui::resolve_theme_mode(nfui::ThemeMode::system))),
          s_(96) {}

    ~SettingsWindow() override {
        if (bg_brush_) DeleteObject(bg_brush_);
    }

    bool create_main(int show_cmd) {
        if (!create({inst_, kClass, kTitle,
                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                     WS_EX_DLGMODALFRAME,
                     CW_USEDEFAULT, CW_USEDEFAULT,
                     s_.logical_to_pixels(kDefaultWidthPx),
                     s_.logical_to_pixels(kDefaultHeightPx),
                     nullptr, nullptr})) {
            return false;
        }

        dpi_ = nfui::dpi_of(hwnd());
        s_   = nfui::DpiScale(dpi_);
        rebuild_layout();
        refresh_running_processes();

        ShowWindow(hwnd(), show_cmd);
        UpdateWindow(hwnd());
        AT_LOG_INFO("Settings window created hwnd=0x%p dpi=%d",
                    static_cast<void*>(hwnd()), dpi_);
        return true;
    }

protected:
    LRESULT handle_message(UINT m, WPARAM w, LPARAM l) override {
        switch (m) {
            case WM_KEYDOWN:   return on_keydown(w);
            case WM_CLOSE:     on_close();                  return 0;
            case WM_ERASEBKGND: {
                HDC dc = reinterpret_cast<HDC>(w);
                RECT rc{};
                GetClientRect(hwnd(), &rc);
                FillRect(dc, &rc, background_brush());
                return 1;
            }
            case WM_CTLCOLORSTATIC: {
                HDC dc = reinterpret_cast<HDC>(w);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, palette_.text_secondary.rgb);
                return reinterpret_cast<LRESULT>(background_brush());
            }
            case WM_CTLCOLOREDIT: {
                HDC dc = reinterpret_cast<HDC>(w);
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, palette_.background.rgb);
                SetTextColor(dc, palette_.text.rgb);
                return reinterpret_cast<LRESULT>(background_brush());
            }
            case WM_DPICHANGED: {
                dpi_ = LOWORD(w);
                s_   = nfui::DpiScale(dpi_);
                // Honor the suggested rect from lParam (per-monitor DPI
                // contract). We keep NOMOVE because the OS already placed
                // the window where it wants it.
                const RECT* suggested = reinterpret_cast<const RECT*>(l);
                SetWindowPos(hwnd(), nullptr,
                             0, 0,
                             suggested->right  - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
                rebuild_layout();
                refresh_hotkey_labels();
                return 0;
            }
        }
        return nfui::Window::handle_message(m, w, l);
    }

    bool on_command(int id, HWND /*src*/, UINT code) override {
        switch (id) {
            case IDC_HK_TILE_CAPTURE:  if (code == BN_CLICKED) { start_capture(CapTile);  return true; } break;
            case IDC_HK_PAUSE_CAPTURE: if (code == BN_CLICKED) { start_capture(CapPause); return true; } break;
            case IDC_PROC_NAME_ADD:    if (code == BN_CLICKED) { on_add_named();         return true; } break;
            case IDC_PROC_PICK_REFRESH:if (code == BN_CLICKED) { refresh_running_processes(); return true; } break;
            case IDC_PROC_FILTER_EDIT: if (code == EN_CHANGE)  { refilter_running_processes(); return true; } break;
            case IDC_PROC_PICK_ADD:    if (code == BN_CLICKED) { on_add_picked();        return true; } break;
            case IDC_PROC_REMOVE:      if (code == BN_CLICKED) { on_remove_selected();   return true; } break;
            case IDC_PROC_LIST:        if (code == LBN_DBLCLK) { on_rename_selected();    return true; } break;
            case IDC_APPLY_BTN:        if (code == BN_CLICKED) { on_apply();             return true; } break;
            case IDC_CANCEL_BTN:       if (code == BN_CLICKED) { on_close();             return true; } break;
            case IDC_OPEN_CONFIG_BTN:  if (code == BN_CLICKED) { UIBridge::open_config_file(); return true; } break;
            case IDC_EXPORT_CONFIG_BTN:if (code == BN_CLICKED) { on_export_config();     return true; } break;
            case IDC_IMPORT_CONFIG_BTN:if (code == BN_CLICKED) { on_import_config();     return true; } break;
            case IDC_EXIT_BTN:         if (code == BN_CLICKED) { on_exit_btn();          return true; } break;
            default: break;
        }
        return false;
    }

private:
    // -------- utilities ------------------------------------------------

    int px(int logical) const { return s_.logical_to_pixels(logical); }

    HBRUSH background_brush() noexcept {
        if (bg_brush_ == nullptr) {
            bg_brush_ = CreateSolidBrush(palette_.background.rgb);
        }
        return bg_brush_;
    }

    // -------- layout rebuild (initial + DPI change) --------------------

    // Tear down every child and rebuild at the current DPI. NFUI controls
    // don't expose a resize hook, so destroy+create is the cleanest path.
    // After rebuild_layout, controls_ is empty so we don't leak the prior
    // tree (each unique_ptr's destructor destroys its HWND).
    void rebuild_layout() {
        controls_.clear();   // every owned HWND; old vectors are gone
        rename_index_ = -1;  // listbox indices are fresh after a rebuild

        int x  = px(kLeftMargin);
        int y  = px(kTopMargin);
        int label_w = px(kLabelW);
        int field_w = px(kFieldW);

        // -- Display row --------------------------------------------------
        add_label(x, y, label_w, px(kRowH), L"&Display", IDC_MONITOR_LABEL);
        add_combo(x + label_w + px(kGapTight + 2), y, field_w,
                  IDC_MONITOR_COMBO, px(kComboDropHeight));
        y += px(kRowH) + px(kGap);

        // -- Processes panel header ---------------------------------------
        add_label(x, y, label_w + px(kGapTight + 2) + field_w, px(kRowH),
                  L"&Processes", IDC_PROCESS_LABEL);
        y += px(kRowH) + px(kGapTight);

        // Row A: free-text name + Add
        add_edit(x + label_w + px(kGapTight + 2), y, field_w - px(kAddBtnW) - px(kGapTight),
                 px(kRowH), IDC_PROC_NAME_EDIT);
        add_button(x + label_w + px(kGapTight + 2) + field_w - px(kAddBtnW), y,
                   px(kAddBtnW), px(kRowH), L"&Add", IDC_PROC_NAME_ADD);
        y += px(kRowH) + px(kGapTight);

        // Row A.5: filter the running-process picker (live substring filter)
        add_label(x, y, label_w, px(kRowH), L"&Filter running",
                  IDC_PROC_FILTER_LABEL);
        add_edit(x + label_w + px(kGapTight + 2), y, field_w, px(kRowH),
                 IDC_PROC_FILTER_EDIT);
        y += px(kRowH) + px(kGapTight);

        // Row B: pick from running processes
        int pick_w = field_w - px(kAddBtnW) - px(kBtnGap) - px(kRefreshBtnW) - px(kGapTight);
        add_combo(x + label_w + px(kGapTight + 2), y, pick_w,
                  IDC_PROC_PICK_COMBO, px(kComboDropHeight));
        add_button(x + label_w + px(kGapTight + 2) + pick_w + px(kGapTight), y,
                   px(kRefreshBtnW), px(kRowH), L"&Refresh", IDC_PROC_PICK_REFRESH);
        add_button(x + label_w + px(kGapTight + 2) + field_w - px(kAddBtnW), y,
                   px(kAddBtnW), px(kRowH), L"A&dd", IDC_PROC_PICK_ADD);
        y += px(kRowH) + px(kGap);

        // Row C: configured ListBox + Remove button to the right
        int list_w = field_w - px(kAddBtnW) - px(kGapTight);
        add_listbox(x + label_w + px(kGapTight + 2), y, list_w, px(kProcListH),
                    IDC_PROC_LIST);
        add_button(x + label_w + px(kGapTight + 2) + list_w + px(kGapTight), y,
                   px(kAddBtnW), px(kProcListH), L"&Remove", IDC_PROC_REMOVE);
        y += px(kProcListH) + px(kGap);

        // -- Padding ------------------------------------------------------
        add_label(x, y, label_w, px(kRowH), L"&Padding (px)", IDC_PADDING_LABEL);
        add_edit(x + label_w + px(kGapTight + 2), y, px(kPaddingFieldW),
                 px(kRowH), IDC_PADDING_EDIT);
        y += px(kRowH) + px(kGap);

        // -- Hotkey rows --------------------------------------------------
        int cap_w  = px(kCaptureBtnW);
        int disp_w = field_w - cap_w - px(kGapTight);
        add_label(x, y, label_w, px(kRowH), L"&Tile-now hotkey", IDC_HK_TILE_LABEL);
        add_edit(x + label_w + px(kGapTight + 2), y, disp_w, px(kRowH),
                 IDC_HK_TILE_DISPLAY, true);
        add_button(x + label_w + px(kGapTight + 2) + disp_w + px(kGapTight), y,
                   cap_w, px(kRowH), L"Captur&e", IDC_HK_TILE_CAPTURE);
        y += px(kRowH) + px(kGap);

        add_label(x, y, label_w, px(kRowH), L"P&ause hotkey", IDC_HK_PAUSE_LABEL);
        add_edit(x + label_w + px(kGapTight + 2), y, disp_w, px(kRowH),
                 IDC_HK_PAUSE_DISPLAY, true);
        add_button(x + label_w + px(kGapTight + 2) + disp_w + px(kGapTight), y,
                   cap_w, px(kRowH), L"Capt&ure", IDC_HK_PAUSE_CAPTURE);
        y += px(kRowH) + px(kGap);

        // -- Autostart ----------------------------------------------------
        add_check(x, y, label_w + px(kGapTight + 2) + field_w, px(kRowH),
                  L"Start with &Windows (auto-launch at logon)",
                  IDC_AUTOSTART_CHECK);
        y += px(kRowH) + px(kGap);

        // -- Log level ----------------------------------------------------
        add_label(x, y, label_w, px(kRowH), L"&Log level", IDC_LOGLEVEL_LABEL);
        add_combo(x + label_w + px(kGapTight + 2), y, px(kLogLevelComboW),
                  IDC_LOGLEVEL_COMBO, px(kComboDropHeight));
        y += px(kRowH) + px(kGap);

        // -- Config file (export / import) --------------------------------
        add_label(x, y, label_w, px(kRowH), L"Config &file", IDC_CFGFILE_LABEL);
        int cf_x = x + label_w + px(kGapTight + 2);
        add_button(cf_x, y, px(kExportBtnW), px(kRowH),
                   L"Export...", IDC_EXPORT_CONFIG_BTN);
        add_button(cf_x + px(kExportBtnW) + px(kBtnGap), y, px(kImportBtnW),
                   px(kRowH), L"Import...", IDC_IMPORT_CONFIG_BTN);
        y += px(kRowH) + px(kGapBeforeButton);

        // -- Buttons row --------------------------------------------------
        int btn_h = px(kRowH) + px(kGapTight);
        int right = x + label_w + px(kGapTight + 2) + field_w;
        add_button(x, y, px(kOpenCfgBtnW), btn_h, L"Open &config file...",
                   IDC_OPEN_CONFIG_BTN);
        add_button(right - 2 * px(kAddBtnW) - px(kBtnGap), y, px(kAddBtnW), btn_h,
                   L"&Apply", IDC_APPLY_BTN);
        add_button(right - px(kAddBtnW), y, px(kAddBtnW), btn_h,
                   L"Cancel", IDC_CANCEL_BTN);
        // Exit between Open-config and the right cluster; secondary style.
        int exit_x = x + px(kOpenCfgBtnW) + px(kBtnGap);
        nfui::ButtonStyle est{};
        est.secondary = true;
        (void)exit_btn_.set_style(est);
        add_button(exit_x, y, px(kExitBtnW), btn_h, L"E&xit AutoTerminal",
                   IDC_EXIT_BTN);

        // -- Initial state ------------------------------------------------
        populate_monitors();
        populate_log_levels();
        apply_text_widgets();
        apply_check_state();
        refresh_hotkey_labels();
        populate_configured_list();
        refilter_running_processes();   // refill picker from cached snapshot
    }

    // -------- control builders ----------------------------------------

    void add_label(int x, int y, int w, int h, std::wstring_view text, int id) {
        nfui::ControlCreateParams p{inst_, hwnd(), id, text, x, y, w, h};
        auto lbl = std::make_unique<nfui::StaticText>();
        (void)lbl->inject_theme(&palette_, &fonts_);
        nfui::TextStyle ts{};
        ts.font_size_pt = nfui::font_pt::sm;
        ts.foreground   = palette_.text_secondary;
        ts.align_v      = nfui::StaticTextAlignV::middle;
        (void)lbl->set_style(ts);
        (void)lbl->create(p);
        controls_.push_back(std::move(lbl));
    }

    void add_edit(int x, int y, int w, int h, int id, bool readonly = false) {
        const DWORD ro_style = readonly ? static_cast<DWORD>(ES_READONLY)
                                        : static_cast<DWORD>(ES_AUTOHSCROLL);
        nfui::ControlCreateParams p{inst_, hwnd(), id, L"",
                                    x, y, w, h,
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ro_style};
        if (id == IDC_PADDING_EDIT) p.style |= ES_NUMBER;
        p.style   |= WS_BORDER;
        p.ex_style = 0;
        auto e = std::make_unique<nfui::Edit>();
        (void)e->inject_theme(&palette_, &fonts_);
        (void)e->create(p);
        HFONT f = (id == IDC_PADDING_EDIT)
                    ? fonts_.mono(dpi_, nfui::font_pt::xs)
                    : fonts_.regular(dpi_, nfui::font_pt::xs);
        SendMessageW(e->hwnd(), WM_SETFONT,
                     reinterpret_cast<WPARAM>(f), TRUE);
        controls_.push_back(std::move(e));
    }

    void add_button(int x, int y, int w, int h, std::wstring_view text, int id) {
        nfui::ControlCreateParams p{inst_, hwnd(), id, text, x, y, w, h};
        nfui::Button& b = button_for(id);
        (void)b.inject_theme(&palette_, &fonts_);
        (void)b.create(p);
    }

    void add_check(int x, int y, int w, int h, std::wstring_view text, int /*id*/) {
        nfui::ControlCreateParams p{inst_, hwnd(), IDC_AUTOSTART_CHECK, text, x, y, w, h};
        (void)auto_check_.inject_theme(&palette_, &fonts_);
        (void)auto_check_.create(p);
    }

    void add_combo(int x, int y, int w, int id, int drop_h) {
        nfui::ControlCreateParams p{inst_, hwnd(), id, L"",
                                    x, y, w, drop_h,
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    CBS_DROPDOWNLIST};
        switch (id) {
            case IDC_MONITOR_COMBO:
                (void)monitor_combo_.inject_theme(&palette_, &fonts_);
                (void)monitor_combo_.create(p);
                break;
            case IDC_PROC_PICK_COMBO:
                (void)proc_pick_combo_.inject_theme(&palette_, &fonts_);
                (void)proc_pick_combo_.create(p);
                break;
            case IDC_LOGLEVEL_COMBO:
                (void)loglevel_combo_.inject_theme(&palette_, &fonts_);
                (void)loglevel_combo_.create(p);
                break;
            default: break;
        }
        HWND h = GetDlgItem(hwnd(), id);
        HFONT f = fonts_.regular(dpi_, nfui::font_pt::xs);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
    }

    void add_listbox(int x, int y, int w, int h, int id) {
        nfui::ControlCreateParams p{inst_, hwnd(), id, L"",
                                    x, y, w, h,
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL |
                                    WS_BORDER};
        (void)proc_list_.inject_theme(&palette_, &fonts_);
        (void)proc_list_.create(p);
        // Install a drag-reorder subclass on the configured-process listbox.
        // Composes with NFUI's own subclass (paint); DefSubclassProc chains on.
        SetWindowSubclass(proc_list_.hwnd(), &SettingsWindow::list_subclass_proc,
                          1, reinterpret_cast<DWORD_PTR>(this));
    }

    // Drag-reorder subclass for IDC_PROC_LIST. Records the anchor item on
    // LBUTTONDOWN, starts reordering after a 4px move threshold, and
    // delete+inserts the anchor at the hovered target on WM_MOUSEMOVE. The
    // new order is read back into cfg_.process_names on Apply.
    static LRESULT CALLBACK list_subclass_proc(HWND h, UINT msg, WPARAM w,
                                               LPARAM l, UINT_PTR /*id*/,
                                               DWORD_PTR ref) {
        auto* self = reinterpret_cast<SettingsWindow*>(ref);
        switch (msg) {
            case WM_LBUTTONDOWN: {
                POINT pt{static_cast<short>(LOWORD(l)),
                          static_cast<short>(HIWORD(l))};
                DWORD hit = static_cast<DWORD>(SendMessageW(
                    h, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y)));
                if (HIWORD(hit) == 0) {
                    self->drag_anchor_index_ = static_cast<int>(LOWORD(hit));
                    self->dragging_ = false;
                    POINT s = pt; ClientToScreen(h, &s);
                    self->drag_start_pt_ = s;
                } else {
                    self->drag_anchor_index_ = -1;
                }
                break;  // fall through to default so selection still updates
            }
            case WM_MOUSEMOVE: {
                if (self->drag_anchor_index_ < 0 || !(w & MK_LBUTTON)) break;
                POINT pt{static_cast<short>(LOWORD(l)),
                          static_cast<short>(HIWORD(l))};
                POINT s = pt; ClientToScreen(h, &s);
                if (!self->dragging_) {
                    int dx = s.x - self->drag_start_pt_.x;
                    int dy = s.y - self->drag_start_pt_.y;
                    if (dx * dx + dy * dy < 16) break;   // <4px threshold
                    self->dragging_ = true;
                }
                DWORD hit = static_cast<DWORD>(SendMessageW(
                    h, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y)));
                if (HIWORD(hit) != 0) break;             // outside client
                int target = static_cast<int>(LOWORD(hit));
                if (target == self->drag_anchor_index_ || target < 0) break;
                wchar_t buf[MAX_PATH]{};
                SendMessageW(h, LB_GETTEXT, self->drag_anchor_index_,
                             reinterpret_cast<LPARAM>(buf));
                SendMessageW(h, LB_DELETESTRING, self->drag_anchor_index_, 0);
                SendMessageW(h, LB_INSERTSTRING, target,
                             reinterpret_cast<LPARAM>(buf));
                SendMessageW(h, LB_SETCURSEL, target, 0);
                self->drag_anchor_index_ = target;
                break;
            }
            case WM_LBUTTONUP:
            case WM_CAPTURECHANGED:
            case WM_RBUTTONDOWN:
                self->drag_anchor_index_ = -1;
                self->dragging_ = false;
                break;
        }
        return DefSubclassProc(h, msg, w, l);
    }

    nfui::Button& button_for(int id) {
        switch (id) {
            case IDC_HK_TILE_CAPTURE:  return btn_tile_cap_;
            case IDC_HK_PAUSE_CAPTURE: return btn_pause_cap_;
            case IDC_APPLY_BTN:        return btn_apply_;
            case IDC_CANCEL_BTN:       return btn_cancel_;
            case IDC_OPEN_CONFIG_BTN:  return btn_open_cfg_;
            case IDC_EXPORT_CONFIG_BTN:return btn_export_;
            case IDC_IMPORT_CONFIG_BTN:return btn_import_;
            case IDC_EXIT_BTN:         return exit_btn_;
            case IDC_PROC_NAME_ADD:    return btn_proc_name_add_;
            case IDC_PROC_PICK_ADD:    return btn_proc_pick_add_;
            case IDC_PROC_PICK_REFRESH:return btn_proc_refresh_;
            case IDC_PROC_REMOVE:      return btn_proc_remove_;
        }
        return btn_apply_;
    }

    // -------- state population ----------------------------------------

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

    // Snapshot the running processes and refill the picker (applying the
    // current filter text). Called on first show, on Refresh, and after a
    // config import.
    void refresh_running_processes() {
        running_proc_cache_ = enumerate_running_process_names();
        refilter_running_processes();
        AT_LOG_DEBUG("Refreshed running-process picker: %zu entries",
                     running_proc_cache_.size());
    }

    // Rebuild the picker combo from the cached snapshot, keeping only names
    // that contain the current filter text (case-insensitive substring).
    // Called on every EN_CHANGE of the filter edit and at the tail of
    // rebuild_layout, so a DPI change restores the list without a fresh
    // Toolhelp32 snapshot.
    void refilter_running_processes() {
        HWND combo = GetDlgItem(hwnd(), IDC_PROC_PICK_COMBO);
        if (!combo) return;
        std::wstring filter;
        HWND fed = GetDlgItem(hwnd(), IDC_PROC_FILTER_EDIT);
        if (fed) get_edit_text(fed, filter);
        filter = trim_ws(filter);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (const auto& n : running_proc_cache_) {
            if (!contains_ci(n, filter)) continue;
            SendMessageW(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(n.c_str()));
        }
        if (SendMessageW(combo, CB_GETCOUNT, 0, 0) > 0) {
            SendMessageW(combo, CB_SETCURSEL, 0, 0);
        }
    }

    void populate_configured_list() {
        HWND lb = GetDlgItem(hwnd(), IDC_PROC_LIST);
        if (!lb) return;
        SendMessageW(lb, LB_RESETCONTENT, 0, 0);
        for (const auto& n : cfg_.process_names) {
            SendMessageW(lb, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(n.c_str()));
        }
    }

    void on_add_named() {
        std::wstring txt;
        HWND edit = GetDlgItem(hwnd(), IDC_PROC_NAME_EDIT);
        if (!get_edit_text(edit, txt)) return;
        std::wstring name = trim_ws(txt);

        // Rename mode: the Add button is relabeled "Rename" while a listbox
        // entry is being edited. Commit replaces that entry in place; an
        // empty name or Esc cancels.
        if (rename_index_ >= 0) {
            HWND lb = GetDlgItem(hwnd(), IDC_PROC_LIST);
            if (!name.empty()) {
                int n = static_cast<int>(SendMessageW(lb, LB_GETCOUNT, 0, 0));
                for (int i = 0; i < n; ++i) {
                    if (i == rename_index_) continue;
                    wchar_t buf[MAX_PATH]{};
                    SendMessageW(lb, LB_GETTEXT, i, reinterpret_cast<LPARAM>(buf));
                    if (name == buf) {
                        AT_LOG_DEBUG("Rename rejected, name in use: %ls",
                                     name.c_str());
                        cancel_rename_mode();
                        return;
                    }
                }
                SendMessageW(lb, LB_DELETESTRING, rename_index_, 0);
                SendMessageW(lb, LB_INSERTSTRING, rename_index_,
                             reinterpret_cast<LPARAM>(name.c_str()));
                SendMessageW(lb, LB_SETCURSEL, rename_index_, 0);
            }
            cancel_rename_mode();
            return;
        }

        if (name.empty()) return;
        add_to_configured_list(name);
        SetWindowTextW(edit, L"");
        SetFocus(edit);
    }

    void on_add_picked() {
        HWND combo = GetDlgItem(hwnd(), IDC_PROC_PICK_COMBO);
        int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
        if (sel < 0) return;
        wchar_t buf[MAX_PATH]{};
        SendMessageW(combo, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(buf));
        std::wstring name = buf;
        if (name.empty()) return;
        add_to_configured_list(name);
    }

    void on_remove_selected() {
        cancel_rename_mode();   // a stale rename index must not survive a delete
        HWND lb = GetDlgItem(hwnd(), IDC_PROC_LIST);
        int sel = static_cast<int>(SendMessageW(lb, LB_GETCURSEL, 0, 0));
        if (sel < 0) return;
        SendMessageW(lb, LB_DELETESTRING, sel, 0);
        int n = static_cast<int>(SendMessageW(lb, LB_GETCOUNT, 0, 0));
        if (n == 0) {
            SetFocus(lb);
            return;
        }
        int new_sel = sel < n ? sel : n - 1;
        SendMessageW(lb, LB_SETCURSEL, new_sel, 0);
    }

    // Double-click a configured entry to rename it: load the name into the
    // Row A edit, relabel the Add button to "Rename", and select-all so a
    // new name can be typed over. Commit via the (Rename) button; Esc cancels.
    void on_rename_selected() {
        HWND lb = GetDlgItem(hwnd(), IDC_PROC_LIST);
        int sel = static_cast<int>(SendMessageW(lb, LB_GETCURSEL, 0, 0));
        if (sel < 0) return;
        wchar_t buf[MAX_PATH]{};
        SendMessageW(lb, LB_GETTEXT, sel, reinterpret_cast<LPARAM>(buf));
        rename_index_ = sel;
        HWND edit = GetDlgItem(hwnd(), IDC_PROC_NAME_EDIT);
        SetWindowTextW(edit, buf);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SetFocus(edit);
        SetWindowTextW(GetDlgItem(hwnd(), IDC_PROC_NAME_ADD), L"Rename");
    }

    // Exit rename mode: relabel the button back to "Add" and clear the edit.
    void cancel_rename_mode() {
        if (rename_index_ < 0) return;
        rename_index_ = -1;
        SetWindowTextW(GetDlgItem(hwnd(), IDC_PROC_NAME_ADD), L"&Add");
        SetWindowTextW(GetDlgItem(hwnd(), IDC_PROC_NAME_EDIT), L"");
    }

    void add_to_configured_list(const std::wstring& name) {
        HWND lb = GetDlgItem(hwnd(), IDC_PROC_LIST);
        if (listbox_contains(lb, name)) {
            AT_LOG_DEBUG("Process already in configured list: %ls", name.c_str());
            return;
        }
        SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        int n = static_cast<int>(SendMessageW(lb, LB_GETCOUNT, 0, 0));
        SendMessageW(lb, LB_SETCURSEL, n - 1, 0);
    }

    void apply_text_widgets() {
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
        if (cap_ != CapNone) {
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
        // Esc cancels an in-progress rename (button reverts to "Add").
        if (rename_index_ >= 0 && vk == VK_ESCAPE) {
            cancel_rename_mode();
            return 0;
        }
        return 1;
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
        cancel_rename_mode();   // discard any uncommitted rename before reading
        cfg_.process_names.clear();
        HWND lb = GetDlgItem(hwnd(), IDC_PROC_LIST);
        int n = static_cast<int>(SendMessageW(lb, LB_GETCOUNT, 0, 0));
        for (int i = 0; i < n; ++i) {
            wchar_t buf[MAX_PATH]{};
            SendMessageW(lb, LB_GETTEXT, i, reinterpret_cast<LPARAM>(buf));
            std::wstring name = trim_ws(buf);
            if (!name.empty()) cfg_.process_names.push_back(std::move(name));
        }
        if (cfg_.process_names.empty()) {
            cfg_.process_names.push_back(L"WindowsTerminal.exe");
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
        cancel_rename_mode();
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

    // Export the on-disk config.toml to a user-chosen path. We copy the live
    // config file rather than the dialog's in-memory cfg_ so "Export" means
    // "back up exactly what the daemon is using right now" — no surprises from
    // un-Applied edits. If no on-disk file exists yet, fall back to writing
    // cfg_ so the export is never empty.
    void on_export_config() {
        std::wstring target;
        std::wstring dir = config_dir().wstring();
        if (!pick_save_path(hwnd(), dir, target)) return;
        std::error_code ec;
        const auto src = config_path();
        if (std::filesystem::exists(src, ec)) {
            std::filesystem::copy_file(src, std::filesystem::path(target),
                std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                AT_LOG_ERROR("Export copy_file failed: %s", ec.message().c_str());
                MessageBoxW(hwnd(),
                    L"Export failed: could not copy the config file.",
                    L"Export config", MB_ICONERROR | MB_OK);
                return;
            }
        } else {
            save_config(std::filesystem::path(target), cfg_);
        }
        AT_LOG_INFO("Exported config to %ls", target.c_str());
        MessageBoxW(hwnd(), L"Config exported successfully.",
                    L"Export config", MB_ICONINFORMATION | MB_OK);
    }

    // Import a chosen config.toml into the dialog for review. The user must
    // click Apply to commit it to the daemon / disk — Import never writes.
    void on_import_config() {
        std::wstring picked;
        std::wstring dir = config_dir().wstring();
        if (!pick_open_path(hwnd(), dir, picked)) return;
        auto loaded = load_config(std::filesystem::path(picked));
        if (!loaded) {
            AT_LOG_WARN("Import parse failed: %ls", picked.c_str());
            MessageBoxW(hwnd(),
                L"Import failed: the selected file is not a valid "
                L"AutoTerminal config.",
                L"Import config", MB_ICONERROR | MB_OK);
            return;
        }
        cfg_ = std::move(*loaded);
        pending_tile_.reset();
        pending_pause_.reset();
        populate_monitors();
        populate_log_levels();
        apply_text_widgets();
        apply_check_state();
        refresh_hotkey_labels();
        populate_configured_list();
        AT_LOG_INFO("Imported config from %ls (review and Apply to commit)",
                    picked.c_str());
        MessageBoxW(hwnd(),
            L"Config imported. Review the settings and click Apply to commit.",
            L"Import config", MB_ICONINFORMATION | MB_OK);
    }

    Hotkey tile_hk()  const { return pending_tile_  ? *pending_tile_  : cfg_.hotkey_tile; }
    Hotkey pause_hk() const { return pending_pause_ ? *pending_pause_ : cfg_.hotkey_toggle_pause; }

    // -------- members -------------------------------------------------

    HINSTANCE inst_{};
    Config     cfg_{};
    SettingsCallbacks cbs_{};
    int        dpi_{96};
    nfui::DpiScale s_{96};
    CaptureState cap_{CapNone};
    std::optional<Hotkey> pending_tile_;
    std::optional<Hotkey> pending_pause_;
    std::vector<std::wstring> running_proc_cache_;   // last Toolhelp32 snapshot
    int  rename_index_ = -1;        // listbox index being renamed (-1 = add mode)
    int  drag_anchor_index_ = -1;   // drag-reorder anchor item, -1 = none
    bool dragging_ = false;         // true once the drag passed the 4px threshold
    POINT drag_start_pt_{};         // LBUTTONDOWN screen point (threshold test)

    nfui::ThemePalette palette_{};
    nfui::FontCache    fonts_{};
    HBRUSH             bg_brush_{nullptr};

    // NFUI controls that need stable identity (so we can style them or read
    // their values) live as named members. Transient labels / edits / generic
    // buttons go through controls_ and are rebuilt on DPI change.
    std::vector<std::unique_ptr<nfui::Control>> controls_;
    nfui::Button btn_tile_cap_{};
    nfui::Button btn_pause_cap_{};
    nfui::Button btn_apply_{};
    nfui::Button btn_cancel_{};
    nfui::Button btn_open_cfg_{};
    nfui::Button btn_export_{};
    nfui::Button btn_import_{};
    nfui::Button exit_btn_{};
    nfui::Button btn_proc_name_add_{};
    nfui::Button btn_proc_pick_add_{};
    nfui::Button btn_proc_refresh_{};
    nfui::Button btn_proc_remove_{};
    nfui::CheckBox auto_check_{};
    nfui::ComboBox monitor_combo_{};
    nfui::ComboBox proc_pick_combo_{};
    nfui::ComboBox loglevel_combo_{};
    nfui::ListBox  proc_list_{};
};

} // namespace

// Public-facing default size helper so main.cpp's initial ShowWindow matches
// the dialog's logical dimensions at the host's current DPI.
SettingsWindowDefaultSize default_settings_window_size(int dpi) noexcept {
    nfui::DpiScale s(dpi);
    SettingsWindowDefaultSize sz{};
    sz.cx = s.logical_to_pixels(kDefaultWidthPx);
    sz.cy = s.logical_to_pixels(kDefaultHeightPx);
    return sz;
}

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