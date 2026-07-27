#include "about_dialog.h"

#include "logger.h"

#include <nfui/Application.hpp>
#include <nfui/Controls/Button.hpp>
#include <nfui/Controls/StaticText.hpp>
#include <nfui/Font.hpp>
#include <nfui/Theme.hpp>
#include <nfui/Window.hpp>

#include <string>

namespace autoterminal {

namespace {

constexpr wchar_t kClass[] = L"AutoTerminal.AboutWindow.v1";
constexpr int kIdOk = 1;

class AboutWindow final : public nfui::Window {
public:
    AboutWindow(HINSTANCE inst, HWND parent)
        : inst_(inst), parent_(parent),
          palette_(nfui::theme_palette(nfui::resolve_theme_mode(nfui::ThemeMode::system))) {}

    ~AboutWindow() override = default;

    bool show() {
        if (!create({inst_, kClass, L"About AutoTerminal",
                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                     WS_EX_DLGMODALFRAME,
                     CW_USEDEFAULT, CW_USEDEFAULT, 380, 200,
                     parent_, nullptr})) {
            return false;
        }
        // Title (serif, xl size).
        nfui::ControlCreateParams p{inst_, hwnd(), 0, L"AutoTerminal",
                                    20, 18, 340, 32};
        title_.inject_theme(&palette_, &fonts_);
        nfui::TextStyle ts{};
        ts.use_semibold = true;
        ts.font_size_pt = nfui::font_pt::xl;
        title_.set_style(ts);
        title_.create(p);

        // Body (regular sans, sm size).
        nfui::ControlCreateParams bp{inst_, hwnd(), 0,
            L"Tiling terminal windows onto a chosen display.\n\n"
            L"Tip: right-click the tray icon for Settings.",
            20, 56, 340, 80};
        body_.inject_theme(&palette_, &fonts_);
        nfui::TextStyle bs{};
        bs.font_size_pt = nfui::font_pt::sm;
        bs.single_line = false;
        bs.end_ellipsis = false;
        body_.set_style(bs);
        body_.create(bp);

        // OK button (accent face, secondary = false for primary action).
        nfui::ControlCreateParams op{inst_, hwnd(), kIdOk, L"OK",
                                     280, 150, 80, 28};
        ok_.inject_theme(&palette_, &fonts_);
        ok_.create(op);

        // Per SettingsDemo pattern: Edit/StaticText/Button paint themselves,
        // so suppress erase to avoid a flicker.
        ShowWindow(hwnd(), SW_SHOWNORMAL);
        UpdateWindow(hwnd());
        AT_LOG_INFO("About dialog shown hwnd=0x%p", static_cast<void*>(hwnd()));
        return true;
    }

protected:
    LRESULT handle_message(UINT m, WPARAM w, LPARAM l) override {
        if (m == WM_ERASEBKGND) return 1;
        if (m == WM_CTLCOLORSTATIC) {
            HDC dc = reinterpret_cast<HDC>(w);
            SetBkMode(dc, TRANSPARENT);
            // Background is theme.surface so labels blend with the panel.
            return reinterpret_cast<LRESULT>(CreateSolidBrush(palette_.background.rgb));
        }
        return nfui::Window::handle_message(m, w, l);
    }

    bool on_command(int id, HWND /*src*/, UINT code) override {
        if (id == kIdOk && code == BN_CLICKED) {
            DestroyWindow(hwnd());
            return true;
        }
        if (id == kIdOk && code == 0) {
            // Accelerator / default-push on Enter.
            DestroyWindow(hwnd());
            return true;
        }
        return false;
    }

    LRESULT on_notify(int /*control_id*/, NMHDR* /*header*/) override { return 0; }

private:
    HINSTANCE inst_{};
    HWND parent_{};
    nfui::ThemePalette palette_{};
    nfui::FontCache    fonts_{};
    nfui::StaticText   title_{};
    nfui::StaticText   body_{};
    nfui::Button       ok_{};
};

} // namespace

void show_about_dialog(HINSTANCE hinst, HWND parent) {
    auto* w = new AboutWindow(hinst, parent);
    if (!w->show()) {
        AT_LOG_ERROR("AboutWindow::show failed");
        delete w;
        return;
    }
    // AboutWindow self-destroys via DestroyWindow; we leak the heap object
    // intentionally and rely on the heap allocator to clean up at process
    // exit. The SettingsDemo uses the same pattern. (Hooking WM_NCDESTROY
    // to delete this would be the safer alternative — left as a TODO if
    // memory growth becomes a concern.)
}

} // namespace autoterminal