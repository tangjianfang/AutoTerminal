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

    ~AboutWindow() override {
        if (bg_brush_) DeleteObject(bg_brush_);
    }

    bool show() {
        if (!create({inst_, kClass, L"About AutoTerminal",
                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                     WS_EX_DLGMODALFRAME,
                     CW_USEDEFAULT, CW_USEDEFAULT, 380, 200,
                     parent_, nullptr})) {
            return false;
        }
        // Title (lg size, semibold, warm-ink colour so it doesn't look fake-black).
        nfui::ControlCreateParams p{inst_, hwnd(), 0, L"AutoTerminal",
                                    20, 18, 340, 32};
        (void)title_.inject_theme(&palette_, &fonts_);
        nfui::TextStyle ts{};
        ts.use_semibold = true;
        ts.font_size_pt = nfui::font_pt::lg;
        ts.foreground   = palette_.text;
        (void)title_.set_style(ts);
        (void)title_.create(p);

        // Body — base size + text_secondary for an understated description.
        nfui::ControlCreateParams bp{inst_, hwnd(), 0,
            L"Tiling terminal windows onto a chosen display.\n\n"
            L"Tip: right-click the tray icon for Settings.",
            20, 56, 340, 80};
        (void)body_.inject_theme(&palette_, &fonts_);
        nfui::TextStyle bs{};
        bs.font_size_pt = nfui::font_pt::base;
        bs.foreground   = palette_.text_secondary;
        bs.single_line  = false;
        bs.end_ellipsis = false;
        (void)body_.set_style(bs);
        (void)body_.create(bp);

        // OK button (accent face, secondary = false for primary action).
        nfui::ControlCreateParams op{inst_, hwnd(), kIdOk, L"OK",
                                     280, 150, 80, 28};
        (void)ok_.inject_theme(&palette_, &fonts_);
        (void)ok_.create(op);

        // Per SettingsDemo pattern: Edit/StaticText/Button paint themselves,
        // so suppress erase to avoid a flicker.
        ShowWindow(hwnd(), SW_SHOWNORMAL);
        UpdateWindow(hwnd());
        AT_LOG_INFO("About dialog shown hwnd=0x%p", static_cast<void*>(hwnd()));
        return true;
    }

protected:
    LRESULT handle_message(UINT m, WPARAM w, LPARAM l) override {
        if (m == WM_ERASEBKGND) {
            // Match the panel surface so the StaticText titles blend in
            // (NFUI Window otherwise paints COLOR_WINDOW — a cold grey).
            HDC dc = reinterpret_cast<HDC>(w);
            RECT rc{};
            GetClientRect(hwnd(), &rc);
            FillRect(dc, &rc, bg_brush());
            return 1;
        }
        if (m == WM_CTLCOLORSTATIC) {
            HDC dc = reinterpret_cast<HDC>(w);
            SetBkMode(dc, TRANSPARENT);
            // text_secondary is a warm grey (#6B6862 light) that reads as
            // ink-on-cream instead of harsh near-black on white.
            SetTextColor(dc, palette_.text_secondary.rgb);
            return reinterpret_cast<LRESULT>(bg_brush());
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
    HBRUSH bg_brush() noexcept {
        if (!bg_brush_) bg_brush_ = CreateSolidBrush(palette_.background.rgb);
        return bg_brush_;
    }

    HINSTANCE inst_{};
    HWND parent_{};
    nfui::ThemePalette palette_{};
    nfui::FontCache    fonts_{};
    HBRUSH             bg_brush_{nullptr};
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