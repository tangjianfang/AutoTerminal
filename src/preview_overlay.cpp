#include "preview_overlay.h"

#include "event_source.h"        // kPreviewTimerId
#include "logger.h"
#include "window_manager.h"      // collect_terminal_windows

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace autoterminal {

namespace {

constexpr wchar_t kOverlayClass[] = L"AutoTerminal.PreviewOverlay.v1";
constexpr UINT kPreviewAutoHideMs = 4000;
constexpr int kBorderPx = 2;       // opaque cell border, logical px @96dpi
constexpr int kLabelHPx  = 20;     // label pill height, logical px @96dpi
constexpr int kLabelPadPx = 6;     // label pill inset from cell corner

// Fixed accent palette. Each configured process gets a stable color hashed
// from its name so the same process always previews in the same hue.
struct Accent { COLORREF rgb; };
constexpr Accent kAccents[] = {
    { RGB( 56, 132, 220) },   // blue
    { RGB( 60, 170,  90) },   // green
    { RGB(214, 138,  40) },   // orange
    { RGB(150,  90, 200) },   // purple
    { RGB( 30, 160, 170) },   // teal
    { RGB(200,  70, 120) },   // pink
    { RGB(120, 110,  90) },   // taupe
    { RGB( 80, 110, 200) },   // indigo
};
constexpr size_t kAccentCount = sizeof(kAccents) / sizeof(kAccents[0]);

Accent accent_for(const std::wstring& name) {
    size_t h = 0;
    for (wchar_t c : name) h = h * 131u + static_cast<size_t>(c);
    return kAccents[h % kAccentCount];
}

int scale_px(int logical, int dpi) {
    return (logical * dpi + 48) / 96;   // matches nfui::DpiScale::logical_to_pixels
}

// One thing to draw on a monitor's overlay: a cell plus the rule that owns it.
// `draw_label` is true only for the first cell of a rule on this monitor so
// overlapping monocle cells don't stack identical labels.
struct CellDraw {
    Rect cell;
    std::wstring name;
    int window_count = 0;
    bool draw_label = false;
};

void register_overlay_class(HINSTANCE inst) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = inst;
    wc.lpszClassName = kOverlayClass;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            AT_LOG_ERROR("PreviewOverlay RegisterClassEx failed err=%lu", err);
            return;
        }
    }
    registered = true;
}

// Composite a premultiplied source-over pixel onto the 32bpp top-down DIB
// (layout [B, G, R, A] per pixel). Source-over — not flat overwrite — so that
// multiple rules sharing a monitor blend their tints instead of the last one
// hiding every earlier one, and overlapping monocle cells stack visibly. On the
// initial zeroed buffer the first write is byte-identical to a flat write
// (destination alpha 0), so single-rule renders are unchanged. Opaque sources
// (a=255, inv=0) still solidly overwrite their pixels.
inline void put_pixel(BYTE* bits, int stride, int x, int y,
                      BYTE r, BYTE g, BYTE b, BYTE a) {
    BYTE* p = bits + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
    int inv = 255 - a;
    int sr = r * a / 255, sg = g * a / 255, sb = b * a / 255;
    int da = p[3];
    p[0] = static_cast<BYTE>(sb + p[0] * inv / 255);
    p[1] = static_cast<BYTE>(sg + p[1] * inv / 255);
    p[2] = static_cast<BYTE>(sr + p[2] * inv / 255);
    p[3] = static_cast<BYTE>(a + da * inv / 255);
}

// Fill an axis-aligned rect with a premultiplied translucent color.
void fill_rect_premult(BYTE* bits, int stride, int w, int h,
                       int rx, int ry, int rw, int rh,
                       BYTE r, BYTE g, BYTE b, BYTE a) {
    int x0 = std::max(0, rx), x1 = std::min(w, rx + rw);
    int y0 = std::max(0, ry), y1 = std::min(h, ry + rh);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            put_pixel(bits, stride, x, y, r, g, b, a);
}

// Force alpha=255 on a rect after GDI DrawText on the 32bpp BI_RGB DIB. GDI
// writes straight (non-premultiplied) RGB and leaves alpha=0 on glyph pixels,
// so we must NOT touch RGB here (the text color is already correct) — only
// mark the rect opaque so UpdateLayeredWindow (AC_SRC_ALPHA, premultiplied)
// composites it solidly. Un-premultiplying or zeroing RGB would turn white
// label text black.
void force_opaque_rect(BYTE* bits, int stride, int w, int h,
                       int rx, int ry, int rw, int rh) {
    int x0 = std::max(0, rx), x1 = std::min(w, rx + rw);
    int y0 = std::max(0, ry), y1 = std::min(h, ry + rh);
    for (int y = y0; y < y1; ++y) {
        BYTE* row = bits + static_cast<size_t>(y) * stride;
        for (int x = x0; x < x1; ++x) {
            row[static_cast<size_t>(x) * 4 + 3] = 255;   // keep GDI's RGB; mark opaque
        }
    }
}

void paint_overlay(HWND hwnd, const RECT& win_rect, int dpi,
                   const std::vector<CellDraw>& draws) {
    int w = win_rect.right - win_rect.left;
    int h = win_rect.bottom - win_rect.top;
    if (w <= 0 || h <= 0) return;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;       // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    HDC mem    = CreateCompatibleDC(screen);
    BYTE* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS,
                                   reinterpret_cast<void**>(&bits), nullptr, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        AT_LOG_ERROR("PreviewOverlay: CreateDIBSection failed");
        return;
    }
    HGDIOBJ old_bmp = SelectObject(mem, dib);
    const int stride = w * 4;

    // Start fully transparent.
    std::memset(bits, 0, static_cast<size_t>(stride) * h);

    int border  = scale_px(kBorderPx, dpi);
    int label_h = scale_px(kLabelHPx, dpi);
    int label_pad = scale_px(kLabelPadPx, dpi);
    int font_h_px = scale_px(14, dpi);

    HFONT font = CreateFontW(-font_h_px, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ old_font = font ? SelectObject(mem, font) : nullptr;
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));

    // Pass 1: fill every cell's translucent body + opaque border. put_pixel
    // composites source-over, so overlapping rules on a shared monitor blend
    // their tints and monocle cells stack; opaque borders still solidly write
    // their 2px frame.
    for (const auto& d : draws) {
        // Cells are absolute virtual-screen coords; localize to this window.
        int cx = d.cell.x - win_rect.left;
        int cy = d.cell.y - win_rect.top;
        int cw = d.cell.w;
        int ch = d.cell.h;
        if (cw <= 0 || ch <= 0) continue;

        Accent acc = accent_for(d.name);
        BYTE r = GetRValue(acc.rgb), g = GetGValue(acc.rgb), b = GetBValue(acc.rgb);

        // Translucent body fill.
        fill_rect_premult(bits, stride, w, h, cx, cy, cw, ch, r, g, b, 160);
        // Opaque border frame on top.
        fill_rect_premult(bits, stride, w, h, cx, cy, cw, border, r, g, b, 255);               // top
        fill_rect_premult(bits, stride, w, h, cx, cy + ch - border, cw, border, r, g, b, 255);  // bottom
        fill_rect_premult(bits, stride, w, h, cx, cy, border, ch, r, g, b, 255);               // left
        fill_rect_premult(bits, stride, w, h, cx + cw - border, cy, border, ch, r, g, b, 255);  // right
    }

    // Pass 2: draw labels on top of ALL fills. A later cell's body/border fill
    // must not erase an earlier cell's label — that matters for Monocle, where
    // every cell of a rule covers the full monitor and only the first carries a
    // label, so drawing the label in-cell would be overwritten by the next cell.
    for (const auto& d : draws) {
        if (!d.draw_label) continue;
        int cx = d.cell.x - win_rect.left;
        int cy = d.cell.y - win_rect.top;
        int cw = d.cell.w;
        int ch = d.cell.h;
        if (cw <= 0 || ch <= 0) continue;

        // Opaque dark pill in the cell's top-left corner.
        int pw = std::min(cw - label_pad * 2, scale_px(220, dpi));
        int ph = label_h;
        int px = cx + label_pad;
        int py = cy + label_pad;
        if (pw <= 0 || ph <= 0) continue;
        fill_rect_premult(bits, stride, w, h, px, py, pw, ph, 24, 24, 28, 255);

        // Draw the label text into the DIB, then force the pill rect opaque so
        // GDI's ClearType alpha doesn't punch transparent holes through it.
        std::wstring label = d.name;
        if (d.window_count > 1) {
            label += L" \x00D7 ";   // U+00D7 multiplication sign
            label += std::to_wstring(d.window_count);
        }
        RECT tr{ px + label_pad / 2, py, px + pw - label_pad / 2, py + ph };
        DrawTextW(mem, label.c_str(), static_cast<int>(label.size()),
                  &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        force_opaque_rect(bits, stride, w, h, px, py, pw, ph);
    }

    if (old_font) SelectObject(mem, old_font);
    if (font) DeleteObject(font);

    SIZE size{ w, h };
    POINT zero{ 0, 0 };
    POINT dst{ win_rect.left, win_rect.top };
    BLENDFUNCTION bf{};
    bf.BlendOp    = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    BOOL ok = UpdateLayeredWindow(hwnd, nullptr, &dst, &size, mem, &zero,
                                  RGB(0, 0, 0), &bf, ULW_ALPHA);
    if (!ok) {
        AT_LOG_WARN("PreviewOverlay: UpdateLayeredWindow failed err=%lu",
                    GetLastError());
    }

    SelectObject(mem, old_bmp);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

} // namespace

OverlayManager::~OverlayManager() { destroy_all(); }

void OverlayManager::destroy_all() {
    for (HWND h : windows_) if (h && IsWindow(h)) DestroyWindow(h);
    windows_.clear();
    visible_ = false;
}

void OverlayManager::hide() {
    if (event_hwnd_) KillTimer(event_hwnd_, kPreviewTimerId);
    destroy_all();
}

void OverlayManager::shutdown() {
    hide();
}

void OverlayManager::show(const Config& cfg) {
    hide();   // idempotent: tear down any prior overlay first

    if (cfg.process_names.empty()) return;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    register_overlay_class(inst);

    auto monitors = enumerate_monitors();
    if (monitors.empty()) {
        AT_LOG_WARN("Preview: no monitors enumerated — nothing to show");
        return;
    }

    // Live window counts per configured rule, then the shared plan.
    std::vector<size_t> all;
    all.reserve(cfg.process_names.size());
    for (size_t i = 0; i < cfg.process_names.size(); ++i) all.push_back(i);

    std::vector<int> counts(cfg.process_names.size(), 0);
    for (size_t i = 0; i < cfg.process_names.size(); ++i) {
        auto wins = collect_terminal_windows({cfg.process_names[i]});
        counts[i] = static_cast<int>(wins.size());
    }

    auto plans = plan_layouts(cfg, monitors, all,
        [&](size_t i) { return i < counts.size() ? counts[i] : 0; });

    // Group cells by the monitor they land on (keyed by monitor index).
    struct MonGroup {
        int mon_index = -1;
        RECT win_rect{};
        std::vector<CellDraw> draws;
    };
    std::vector<MonGroup> groups;
    for (const auto& p : plans) {
        if (!p.monitor || p.layout.cells.empty()) continue;
        // find this monitor's index in the enumerated vector
        int mi = -1;
        for (size_t k = 0; k < monitors.size(); ++k) {
            if (&monitors[k] == p.monitor) { mi = static_cast<int>(k); break; }
        }
        if (mi < 0) continue;
        MonGroup* g = nullptr;
        for (auto& cand : groups) if (cand.mon_index == mi) { g = &cand; break; }
        if (!g) {
            MonGroup ng;
            ng.mon_index = mi;
            ng.win_rect = { p.monitor->rect.x, p.monitor->rect.y,
                            p.monitor->rect.x + p.monitor->rect.w,
                            p.monitor->rect.y + p.monitor->rect.h };
            groups.push_back(std::move(ng));
            g = &groups.back();
        }
        bool first = true;
        for (const auto& cell : p.layout.cells) {
            CellDraw cd;
            cd.cell = cell;
            cd.name = p.name;
            cd.window_count = p.window_count;
            cd.draw_label = first;
            g->draws.push_back(std::move(cd));
            first = false;
        }
    }

    if (groups.empty()) {
        AT_LOG_INFO("Preview: no placeable rules — nothing to show");
        return;
    }

    for (auto& g : groups) {
        DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE
                 | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW;
        HWND hwnd = CreateWindowExW(ex, kOverlayClass, L"AutoTerminal Preview",
                                    WS_POPUP,
                                    g.win_rect.left, g.win_rect.top,
                                    g.win_rect.right - g.win_rect.left,
                                    g.win_rect.bottom - g.win_rect.top,
                                    nullptr, nullptr, inst, nullptr);
        if (!hwnd) {
            AT_LOG_WARN("PreviewOverlay: CreateWindowEx failed err=%lu",
                        GetLastError());
            continue;
        }
        int dpi = GetDpiForWindow(hwnd);
        paint_overlay(hwnd, g.win_rect, dpi, g.draws);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        windows_.push_back(hwnd);
    }

    visible_ = true;
    if (event_hwnd_) {
        SetTimer(event_hwnd_, kPreviewTimerId, kPreviewAutoHideMs, nullptr);
    }
    AT_LOG_INFO("Preview: showing %zu monitor overlay/overlays (%zu rules)",
                windows_.size(), plans.size());
}

} // namespace autoterminal