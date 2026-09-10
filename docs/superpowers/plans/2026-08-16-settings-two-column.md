# Settings two-column layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reshape the Settings dialog into a two-column 880 × 560 layout that fits low-resolution work areas, guaranteed by a pure `fit_rect_in` clamp applied at window creation and on DPI change.

**Architecture:** One pure math function (`fit_rect_in`) lands in `tile_engine` with unit tests; `settings_dialog.cpp` switches `rebuild_layout` to two column-local y-cursors and wires the clamp into `create_main` and `WM_DPICHANGED`. No other module touched.

**Tech Stack:** C++20, raw Win32, NativeFrameUI, GoogleTest.

**Spec:** `docs/superpowers/specs/2026-08-16-settings-two-column-design.md`

## Global Constraints

- Dialog target size: 880 × 560 logical px (constants `kDefaultWidthPx` / `kDefaultHeightPx`).
- Column geometry: left col x=`kLeftMargin`(14) w=360; right col x=390 w=476; column gap 16; right column label 110 + field 360.
- `fit_rect_in` must be pure (no Win32 types beyond the existing `Rect`).
- Hint texts, section names, label wording: unchanged from the previous iteration.
- All 70 existing tests must stay green; new tests only for `fit_rect_in`.

---

### Task 1: `fit_rect_in` in tile_engine (TDD)

**Files:**
- Modify: `src/tile_engine.h` (declaration, after `compute_layout` overloads)
- Modify: `src/tile_engine.cpp` (implementation)
- Test: `tests/test_tile_engine.cpp` (append suite)

**Interfaces:**
- Produces: `Rect fit_rect_in(Rect window, Rect work_area)` — shrink-to-fit then center; consumed by Task 2.

- [ ] **Step 1: Write failing tests** — append to `tests/test_tile_engine.cpp`:

```cpp
TEST(FitRectIn, NoOpWhenItFits) {
    Rect r = fit_rect_in(Rect{100, 50, 580, 650}, Rect{0, 0, 1920, 1040});
    EXPECT_EQ(r.x, 770); EXPECT_EQ(r.y, 245);   // centered in work area
    EXPECT_EQ(r.w, 580); EXPECT_EQ(r.h, 650);   // size untouched
}

TEST(FitRectIn, ShrinksTooTall) {
    Rect r = fit_rect_in(Rect{0, 0, 580, 930}, Rect{0, 0, 1366, 720});
    EXPECT_EQ(r.h, 720);
    EXPECT_EQ(r.w, 580);
    EXPECT_EQ(r.x, (1366 - 580) / 2);
    EXPECT_EQ(r.y, 0);
}

TEST(FitRectIn, ShrinksTooWideAndTall) {
    Rect r = fit_rect_in(Rect{0, 0, 880, 560}, Rect{0, 0, 600, 500});
    EXPECT_EQ(r.w, 600); EXPECT_EQ(r.h, 500);
    EXPECT_EQ(r.x, 0); EXPECT_EQ(r.y, 0);
}

TEST(FitRectIn, EmptyWorkAreaPassthrough) {
    Rect r = fit_rect_in(Rect{10, 10, 400, 300}, Rect{0, 0, 0, 0});
    EXPECT_EQ(r.w, 0); EXPECT_EQ(r.h, 0);   // degenerate work area wins
}
```

- [ ] **Step 2:** `build.bat && build\tests\autoterminal_tests.exe --gtest_filter=FitRectIn.*` → FAIL (not defined).

- [ ] **Step 3: Implement** — `src/tile_engine.h` declaration and `src/tile_engine.cpp`:

```cpp
// Fit `window` inside `work_area` for dialog placement: shrink dimensions
// that don't fit, then center the result in the work area. Pure math —
// callers pass Win32 rects in/out.
Rect fit_rect_in(Rect window, Rect work_area) {
    int w = std::min(window.w, work_area.w);
    int h = std::min(window.h, work_area.h);
    return Rect{work_area.x + (work_area.w - w) / 2,
                work_area.y + (work_area.h - h) / 2, w, h};
}
```

(`#include <algorithm>` already present in tile_engine.cpp; if not, add it.)

- [ ] **Step 4:** Re-run filter → PASS; full suite → 70 + 4 PASS.

- [ ] **Step 5: Commit** — `git add src/tile_engine.* tests/test_tile_engine.cpp && git commit -m "tile_engine: add fit_rect_in (shrink + center into work area)"`

---

### Task 2: two-column `rebuild_layout` + clamp wiring

**Files:**
- Modify: `src/settings_dialog.cpp` — constants block, `create_main`, `WM_DPICHANGED` arm of `handle_message`, `rebuild_layout` body.

**Interfaces:**
- Consumes: `fit_rect_in(Rect, Rect)` (Task 1), existing builders.

- [ ] **Step 1: Constants** — replace size block:

```cpp
constexpr int kDefaultWidthPx  = 880;   // two-column layout
constexpr int kDefaultHeightPx = 560;
constexpr int kColGap          = 16;   // gutter between the two columns
constexpr int kLeftColW        = 360;  // left column width (labels 116 + field 238)
constexpr int kRightColW       = 476;  // right column width (labels 110 + field 360)
```

- [ ] **Step 2: `create_main` clamp** — before `create({...})`, compute the fitted rect from the cursor's monitor:

```cpp
POINT cursor{}; GetCursorPos(&cursor);
HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
MONITORINFO mi{.cbSize = sizeof(mi)};
GetMonitorInfoW(mon, &mi);
autoterminal::Rect fitted = autoterminal::fit_rect_in(
    {0, 0, s_.logical_to_pixels(kDefaultWidthPx),
            s_.logical_to_pixels(kDefaultHeightPx)},
    {mi.rcWork.left, mi.rcWork.top,
     mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top});
```

then pass `fitted.x, fitted.y, fitted.w, fitted.h` instead of `CW_USEDEFAULT, CW_USEDEFAULT, …logical_to_pixels…`.

- [ ] **Step 3: `WM_DPICHANGED` clamp** — replace the current suggested-rect apply:

```cpp
const RECT* suggested = reinterpret_cast<const RECT*>(l);
HMONITOR mon = MonitorFromWindow(hwnd(), MONITOR_DEFAULTTONEAREST);
MONITORINFO mi{.cbSize = sizeof(mi)};
GetMonitorInfoW(mon, &mi);
autoterminal::Rect fitted = autoterminal::fit_rect_in(
    {suggested->left, suggested->top,
     suggested->right - suggested->left, suggested->bottom - suggested->top},
    {mi.rcWork.left, mi.rcWork.top,
     mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top});
SetWindowPos(hwnd(), nullptr, fitted.x, fitted.y, fitted.w, fitted.h,
             SWP_NOZORDER | SWP_NOACTIVATE);
```

(drop `SWP_NOMOVE` — the clamp may need to re-center).

- [ ] **Step 4: two-column `rebuild_layout`** — restructure the form section:

```cpp
        int label_w = px(kLabelW);
        int field_w = px(kFieldW);
        int form_x  = x + label_w + px(kGapTight + 2);   // field column origin
        int full_w  = label_w + px(kGapTight + 2) + field_w;
```

becomes two cursors:

```cpp
        int lx = px(kLeftMargin);            // left column origin
        int rx = lx + px(kLeftColW) + px(kColGap);   // right column origin
        int ly = px(kTopMargin);
        int ry = px(kTopMargin);
        // Left column: label 116 + field 238 = kLeftColW.
        int l_label_w = px(116);
        int l_field_w = px(kLeftColW) - l_label_w - px(kGapTight + 2);
        int l_form_x  = lx + l_label_w + px(kGapTight + 2);
        // Right column: label 110 + field 360 = kRightColW.
        int r_label_w = px(110);
        int r_field_w = px(360);
        int r_form_x  = rx + r_label_w + px(kGapTight + 2);
```

Then: **Display & Layout** and **Hotkeys** sections emit at `(lx, ly)` using
`l_label_w / l_form_x / l_field_w` (capture buttons stay `kCaptureBtnW`;
hotkey display width = `l_field_w - cap_w - px(kGapTight)`; section headers
and hints use width `px(kLeftColW)`; hotkey hint spans `px(kLeftColW)`).
**Processes** and **Startup & General** emit at `(rx, ry)` with
`r_label_w / r_form_x / r_field_w`; headers/hints span `px(kRightColW)`.
After both columns, `int y = std::max(ly, ry);` feeds the existing buttons
row (`right` anchor becomes `rx + r_label_w + px(kGapTight + 2) + r_field_w`).
Initial-state block unchanged.

- [ ] **Step 5:** Build + full suite green.

- [ ] **Step 6: Visual + probe verification** — relaunch, `probe_settings_pos.ps1` reports ≈ 880 × 560 scaled; screenshot shows two columns, hints wrapped within column width, no clipping/overlap; `probe_windows.ps1` still 50 children. On a work area smaller than 880 × 560 (e.g. 600 × 500 windowed-RDP test or temporary), dialog shrinks + centers — verify by logic review of `fit_rect_in` tests (physical small-monitor test optional).

- [ ] **Step 7: Commit** — `git commit -m "settings: two-column layout + work-area fit (880x560)"`

---

### Task 3: finish

- [ ] Full suite + visual re-check, merge to `master`, push, report.
