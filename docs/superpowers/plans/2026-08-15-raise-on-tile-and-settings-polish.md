# Raise-on-tile + Settings text polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After every re-tile pass, raise managed windows as a group to the top of the Z-order (no focus change), and restructure the Settings dialog with section headers, hint lines, and cleaner label wording.

**Architecture:** Feature 1 is a 3-line semantic change inside `window_manager.cpp`'s `apply_layout` (reverse iteration + `HWND_TOP`); every tile trigger path funnels through this function, so coverage is automatic. Feature 2 is confined to `settings_dialog.cpp`: two new StaticText helpers (`add_section_header`, `add_hint`) and a regrouped `rebuild_layout`; the existing destroy-and-recreate DPI rebuild makes them DPI-safe by construction.

**Tech Stack:** C++20, raw Win32, NativeFrameUI (sibling repo `C:/tjf/github/NativeFrameUI`, linked per-component), GoogleTest.

**Spec:** `docs/superpowers/specs/2026-08-15-raise-on-tile-and-settings-polish-design.md`

## Global Constraints

- C++20, MSVC `/W4 /permissive-`, static runtime (`/MT`) — do not add dynamic-runtime dependencies.
- Raise must use `HWND_TOP` (never `HWND_TOPMOST`) and always keep `SWP_NOACTIVATE` — a re-tile never steals focus.
- No new config fields; no config format changes; UI text stays English.
- Mnemonic accelerators (`&x`) among touched labels must be unique: `d` (Target &display), `t` (&Tile now), `u` (Pa&use / resume), `f` (Tile &first process), `v` (Pre&view tiling), `s` (&Start delay).
- Section headers and hints carry **no** mnemonics (no `&`).
- Build entry point is `build.bat` (configures + builds, calls vcvars64 itself).
- Test binary: `build\tests\autoterminal_tests.exe`; single suite: `--gtest_filter=<Suite>.*`.

---

### Task 1: Raise-on-tile in `apply_layout`

**Files:**
- Modify: `src/window_manager.cpp:81-108` (`apply_layout`)
- Test: `tests/test_window_manager_match.cpp` (append)

**Interfaces:**
- Consumes: existing `apply_layout(const std::vector<WindowEntry>&, const Layout&)` (unchanged signature).
- Produces: unchanged signature — behavior contract now "windows are raised as a group to the top of the Z-order, preserving their input-vector relative order; focus untouched".

- [ ] **Step 1: Write the failing test**

Append to `tests/test_window_manager_match.cpp` (after the existing includes/tests; the file already includes `window_manager.h`, `<vector>`, `<string>`, gtest, and `using namespace autoterminal;` — add `#include <windows.h>` at the top, above `#include "window_manager.h"`):

```cpp
// Real top-level windows are required: z-order changes cannot be observed on
// message-only or child windows. Three tiny overlapped windows are created
// non-activated, tiled, then destroyed — safe in an interactive test session.
TEST(ApplyLayout, RaisesGroupToTopPreservingVectorOrder) {
    HWND w[3]{};
    for (auto& h : w) {
        h = CreateWindowExW(0, L"STATIC", L"AutoTerminalTest", WS_OVERLAPPEDWINDOW,
                            0, 0, 120, 80, nullptr, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
        ASSERT_NE(h, nullptr);
        ShowWindow(h, SW_SHOWNOACTIVATE);
    }

    std::vector<WindowEntry> entries;
    for (HWND h : w) entries.push_back(WindowEntry{h, 0, L"AutoTerminalTest.exe"});

    Layout lay;
    lay.rows = 1;
    lay.cols = 3;
    lay.cells = {Rect{0, 0, 100, 60}, Rect{120, 0, 100, 60}, Rect{240, 0, 100, 60}};

    ASSERT_EQ(apply_layout(entries, lay), 3);

    // The whole group is now at the top of the Z-order, and the group's
    // relative order matches the input vector: w[0] highest, then w[1], w[2].
    EXPECT_EQ(GetWindow(w[0], GW_HWNDPREV), nullptr);
    EXPECT_EQ(GetWindow(w[1], GW_HWNDPREV), w[0]);
    EXPECT_EQ(GetWindow(w[2], GW_HWNDPREV), w[1]);

    for (HWND h : w) if (h) DestroyWindow(h);
}
```

- [ ] **Step 2: Build and run the test to verify it fails**

Run: `build.bat && build\tests\autoterminal_tests.exe --gtest_filter=ApplyLayout.*`
Expected: FAIL — with the current `SWP_NOZORDER` implementation the z-order is untouched, so `GetWindow(w[0], GW_HWNDPREV)` returns `w[1]` (creation stacking), not `nullptr`. (If it fails with a different value because another app raised a window mid-test, that still proves the raise is not happening — proceed.)

- [ ] **Step 3: Implement the raise**

In `src/window_manager.cpp`, replace the loop and comment in `apply_layout` (lines 82-106). The loop now runs in reverse and the single `SetWindowPos` per window both moves and raises:

```cpp
int apply_layout(const std::vector<WindowEntry>& windows, const Layout& layout) {
    int placed = 0;
    int n = std::min<int>(static_cast<int>(windows.size()),
                          static_cast<int>(layout.cells.size()));
    // Place in reverse order so successive HWND_TOP inserts preserve the
    // group's relative z-order: EnumWindows yields topmost-first, and each
    // HWND_TOP insert leaves the last-processed window highest, so ending
    // with windows[0] keeps it topmost of the group. The whole group ends
    // above other applications' windows (raise-once: HWND_TOP, NOT
    // HWND_TOPMOST). SWP_NOACTIVATE keeps the current focus untouched.
    for (int i = n - 1; i >= 0; --i) {
        HWND h = windows[i].hwnd;
        const Rect& cell = layout.cells[i];
        if (cell.is_empty()) continue;

        // Auto-restore minimized / maximized windows before positioning.
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(h, &wp) &&
            (wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_SHOWMAXIMIZED)) {
            ShowWindow(h, SW_RESTORE);
        }

        // HWND_TOP (not SWP_NOZORDER) raises the window above other apps
        // while we move it — one call per window, no extra pass.
        if (SetWindowPos(h, HWND_TOP,
                         cell.x, cell.y, cell.w, cell.h,
                         SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
            ++placed;
        } else {
            AT_LOG_WARN("SetWindowPos failed for hwnd=0x%p (gle=%lu)", h, GetLastError());
        }
    }
    return placed;
}
```

- [ ] **Step 4: Run the new test and the full suite**

Run: `build\tests\autoterminal_tests.exe --gtest_filter=ApplyLayout.*`
Expected: PASS
Run: `build\tests\autoterminal_tests.exe`
Expected: all PASS (existing 30+ tests unaffected)

- [ ] **Step 5: Manual verification of the feature end-to-end**

Run `build\AutoTerminal.exe`, then:
1. Open 3 Windows Terminal windows + 1 other app (e.g. Notepad) overlapping them.
2. Trigger re-tiles via: Ctrl+Alt+T (tile now), opening a new terminal, closing a terminal.
3. Expect each time: terminal group ends above Notepad; focus stays where it was (type in Notepad, re-tile, keep typing — no interruption); terminals are NOT always-on-top afterwards (click Notepad's taskbar entry, it covers them normally).

- [ ] **Step 6: Commit**

```bash
git add src/window_manager.cpp tests/test_window_manager_match.cpp
git commit -m "apply_layout: raise tiled group to top of z-order (no focus change)"
```

---

### Task 2: Settings dialog — section-header and hint helpers

**Files:**
- Modify: `src/settings_dialog.cpp` (constants block ~line 39-64, CtrlId enum ~line 66-117, control builders after `add_label` ~line 531)

**Interfaces:**
- Consumes: `nfui::StaticText`, `nfui::TextStyle` (`use_semibold`, `font_size_pt`, `foreground`, `align_v`), `nfui::font_pt::md/xs`, `palette_.text`, `palette_.text_secondary`, `palette_.background`, `nfui::lerp_color(Color, Color, float)` (declared in `nfui/Theme.hpp`, defined in the linked `nfui_theme` target).
- Produces: `void add_section_header(int x, int y, int w, std::wstring_view text, int id)` and `void add_hint(int x, int y, int w, std::wstring_view text, int id)`; new control ids `IDC_SECTION_DISPLAY`, `IDC_SECTION_PROCESSES`, `IDC_SECTION_HOTKEYS`, `IDC_SECTION_STARTUP`, `IDC_HINT_HOTKEYS`, `IDC_HINT_PROCESSES`, `IDC_HINT_INSPECTOR`, `IDC_HINT_PADDING`, `IDC_HINT_START_DELAY` — consumed by Task 3.

- [ ] **Step 1: Add the layout constants**

In the constants block after `kComboDropHeight` (line 64):

```cpp
constexpr int kGapSection      = 10;   // breathing room before each section header
constexpr int kHintH           = 16;   // hint-line height (xs text, single line)
```

- [ ] **Step 2: Add the new control ids**

Append at the end of the `CtrlId` enum (after `IDC_HK_PREVIEW_CAPTURE`, before the closing `};`):

```cpp
    IDC_SECTION_DISPLAY,          // section header "Display & Layout"
    IDC_SECTION_PROCESSES,        // section header "Processes"
    IDC_SECTION_HOTKEYS,          // section header "Hotkeys"
    IDC_SECTION_STARTUP,          // section header "Startup & General"

    IDC_HINT_HOTKEYS,             // capture how-to under Hotkeys header
    IDC_HINT_PROCESSES,           // rename / reorder help under Processes header
    IDC_HINT_INSPECTOR,           // "applies to selected process" hint
    IDC_HINT_PADDING,             // padding meaning
    IDC_HINT_START_DELAY,         // autostart delay meaning
```

- [ ] **Step 3: Add the two builders**

Insert immediately after `add_label` (after line 531). They follow `add_label`'s exact pattern (ControlCreateParams → unique_ptr StaticText → inject_theme → set_style → create → move into `controls_`):

```cpp
    // Section heading: semibold, md (14 pt), primary text color — the top
    // of the dialog's three-level text hierarchy (header / label / hint).
    void add_section_header(int x, int y, int w, std::wstring_view text, int id) {
        nfui::ControlCreateParams p{inst_, hwnd(), id, text, x, y, w, px(kRowH)};
        auto lbl = std::make_unique<nfui::StaticText>();
        (void)lbl->inject_theme(&palette_, &fonts_);
        nfui::TextStyle ts{};
        ts.font_size_pt = nfui::font_pt::md;
        ts.use_semibold = true;
        ts.foreground   = palette_.text;
        ts.align_v      = nfui::StaticTextAlignV::middle;
        (void)lbl->set_style(ts);
        (void)lbl->create(p);
        controls_.push_back(std::move(lbl));
    }

    // Secondary hint line: xs (11 pt), text_secondary faded 35 % toward the
    // dialog background so it reads one step below the form labels (which
    // are text_secondary at sm).
    void add_hint(int x, int y, int w, std::wstring_view text, int id) {
        nfui::ControlCreateParams p{inst_, hwnd(), id, text, x, y, w, px(kHintH)};
        auto lbl = std::make_unique<nfui::StaticText>();
        (void)lbl->inject_theme(&palette_, &fonts_);
        nfui::TextStyle ts{};
        ts.font_size_pt = nfui::font_pt::xs;
        ts.foreground   = nfui::lerp_color(palette_.text_secondary,
                                           palette_.background, 0.35f);
        ts.align_v      = nfui::StaticTextAlignV::middle;
        (void)lbl->set_style(ts);
        (void)lbl->create(p);
        controls_.push_back(std::move(lbl));
    }
```

- [ ] **Step 4: Build to verify it compiles clean**

Run: `build.bat`
Expected: build succeeds, no new warnings (`/W4`). The helpers are unused member functions — MSVC does not warn on those. (If a warning appears, fix it before committing.)

- [ ] **Step 5: Commit**

```bash
git add src/settings_dialog.cpp
git commit -m "settings: add section-header and hint text builders"
```

---

### Task 3: Settings dialog — regroup form, new wording, dialog height

**Files:**
- Modify: `src/settings_dialog.cpp:42-43` (`kDefaultHeightPx`), `rebuild_layout` body (lines 358-516)

**Interfaces:**
- Consumes: `add_section_header` / `add_hint` and the `IDC_SECTION_*` / `IDC_HINT_*` ids from Task 2; all existing row builders and ids.
- Produces: nothing downstream — this is the visible deliverable.

- [ ] **Step 1: Bump the dialog height**

```cpp
constexpr int kDefaultWidthPx  = 580;
constexpr int kDefaultHeightPx = 812;   // +4 section headers, +5 hint lines, section gaps
```

- [ ] **Step 2: Rewrite `rebuild_layout`'s form section**

Replace everything from `int x  = px(kLeftMargin);` (line 362) down to the end of the config-file row (`y += px(kRowH) + px(kGapBeforeButton);`, line 489) with the code below. The buttons row and the `-- Initial state --` block (lines 491-516) are **unchanged**. Coordinate math is identical to the existing style (`px()` everywhere, `form_x` hoisted as a named local to reduce repetition):

```cpp
        int x  = px(kLeftMargin);
        int y  = px(kTopMargin);
        int label_w = px(kLabelW);
        int field_w = px(kFieldW);
        int form_x  = x + label_w + px(kGapTight + 2);   // field column origin
        int full_w  = label_w + px(kGapTight + 2) + field_w;

        // ============ Section: Display & Layout ==========================
        add_section_header(x, y, full_w, L"Display & Layout", IDC_SECTION_DISPLAY);
        y += px(kRowH) + px(kGapTight);

        add_label(x, y, label_w, px(kRowH), L"Target &display", IDC_MONITOR_LABEL);
        add_combo(form_x, y, field_w, IDC_MONITOR_COMBO, px(kComboDropHeight));
        y += px(kRowH) + px(kGap);

        add_label(x, y, label_w, px(kRowH), L"&Padding (px)", IDC_PADDING_LABEL);
        add_edit(form_x, y, px(kPaddingFieldW), px(kRowH), IDC_PADDING_EDIT);
        add_hint(form_x, y + px(kRowH) + px(kGapTight), field_w,
                 L"Gap around each tiled window. 0 = edge-to-edge.",
                 IDC_HINT_PADDING);
        y += px(kRowH) + px(kGapTight) + px(kHintH) + px(kGap);

        // ============ Section: Processes ==================================
        y += px(kGapSection);
        add_section_header(x, y, full_w, L"Processes", IDC_SECTION_PROCESSES);
        add_hint(form_x, y + px(kRowH) + px(kGapTight), field_w,
                 L"Matched by process name. Double-click a row to rename; "
                 L"drag to reorder.",
                 IDC_HINT_PROCESSES);
        y += px(kRowH) + px(kGapTight) + px(kHintH) + px(kGapTight);

        // Row A: free-text name + Add
        add_edit(form_x, y, field_w - px(kAddBtnW) - px(kGapTight),
                 px(kRowH), IDC_PROC_NAME_EDIT);
        add_button(form_x + field_w - px(kAddBtnW), y,
                   px(kAddBtnW), px(kRowH), L"&Add", IDC_PROC_NAME_ADD);
        y += px(kRowH) + px(kGapTight);

        // Row A.5: filter the running-process picker (live substring filter)
        add_label(x, y, label_w, px(kRowH), L"&Filter running",
                  IDC_PROC_FILTER_LABEL);
        add_edit(form_x, y, field_w, px(kRowH), IDC_PROC_FILTER_EDIT);
        y += px(kRowH) + px(kGapTight);

        // Row B: pick from running processes
        int pick_w = field_w - px(kAddBtnW) - px(kBtnGap) - px(kRefreshBtnW) - px(kGapTight);
        add_combo(form_x, y, pick_w, IDC_PROC_PICK_COMBO, px(kComboDropHeight));
        add_button(form_x + pick_w + px(kGapTight), y,
                   px(kRefreshBtnW), px(kRowH), L"&Refresh", IDC_PROC_PICK_REFRESH);
        add_button(form_x + field_w - px(kAddBtnW), y,
                   px(kAddBtnW), px(kRowH), L"A&dd", IDC_PROC_PICK_ADD);
        y += px(kRowH) + px(kGap);

        // Row C: configured ListBox + Remove button to the right
        int list_w = field_w - px(kAddBtnW) - px(kGapTight);
        add_listbox(form_x, y, list_w, px(kProcListH), IDC_PROC_LIST);
        add_button(form_x + list_w + px(kGapTight), y,
                   px(kAddBtnW), px(kProcListH), L"&Remove", IDC_PROC_REMOVE);
        y += px(kProcListH) + px(kGap);

        // Per-row inspector: layout mode + monitor for the selected row.
        // Disabled when nothing is selected; LBN_SELCHANGE refreshes them
        // from live_rules_.
        add_label(x, y, label_w, px(kRowH), L"La&yout", IDC_PROC_LAYOUT_LABEL);
        add_combo(form_x, y, field_w, IDC_PROC_LAYOUT_COMBO, px(kComboDropHeight));
        y += px(kRowH) + px(kGapTight);

        add_label(x, y, label_w, px(kRowH), L"Mo&nitor", IDC_PROC_MONITOR_LABEL);
        add_combo(form_x, y, field_w, IDC_PROC_MONITOR_COMBO, px(kComboDropHeight));
        y += px(kRowH) + px(kGapTight);

        add_hint(form_x, y, field_w,
                 L"Applies to the process selected above.", IDC_HINT_INSPECTOR);
        y += px(kHintH) + px(kGap);

        // ============ Section: Hotkeys ====================================
        y += px(kGapSection);
        add_section_header(x, y, full_w, L"Hotkeys", IDC_SECTION_HOTKEYS);
        add_hint(form_x, y + px(kRowH) + px(kGapTight), field_w,
                 L"Click Capture, then press the key combination. Esc cancels.",
                 IDC_HINT_HOTKEYS);
        y += px(kRowH) + px(kGapTight) + px(kHintH) + px(kGapTight);

        int cap_w  = px(kCaptureBtnW);
        int disp_w = field_w - cap_w - px(kGapTight);
        add_label(x, y, label_w, px(kRowH), L"&Tile now", IDC_HK_TILE_LABEL);
        add_edit(form_x, y, disp_w, px(kRowH), IDC_HK_TILE_DISPLAY, true);
        add_button(form_x + disp_w + px(kGapTight), y,
                   cap_w, px(kRowH), L"Captur&e", IDC_HK_TILE_CAPTURE);
        y += px(kRowH) + px(kGap);

        add_label(x, y, label_w, px(kRowH), L"Pa&use / resume", IDC_HK_PAUSE_LABEL);
        add_edit(form_x, y, disp_w, px(kRowH), IDC_HK_PAUSE_DISPLAY, true);
        add_button(form_x + disp_w + px(kGapTight), y,
                   cap_w, px(kRowH), L"Capt&ure", IDC_HK_PAUSE_CAPTURE);
        y += px(kRowH) + px(kGap);

        add_label(x, y, label_w, px(kRowH), L"Tile &first process",
                  IDC_HK_TILESPEC_LABEL);
        add_edit(form_x, y, disp_w, px(kRowH), IDC_HK_TILESPEC_DISPLAY, true);
        add_button(form_x + disp_w + px(kGapTight), y,
                   cap_w, px(kRowH), L"Cap&ture", IDC_HK_TILESPEC_CAPTURE);
        y += px(kRowH) + px(kGap);

        add_label(x, y, label_w, px(kRowH), L"Pre&view tiling",
                  IDC_HK_PREVIEW_LABEL);
        add_edit(form_x, y, disp_w, px(kRowH), IDC_HK_PREVIEW_DISPLAY, true);
        add_button(form_x + disp_w + px(kGapTight), y,
                   cap_w, px(kRowH), L"Capt&ure", IDC_HK_PREVIEW_CAPTURE);
        y += px(kRowH) + px(kGap);

        // ============ Section: Startup & General ==========================
        y += px(kGapSection);
        add_section_header(x, y, full_w, L"Startup & General", IDC_SECTION_STARTUP);
        y += px(kRowH) + px(kGapTight);

        add_check(x, y, label_w + px(kGapTight + 2) + field_w, px(kRowH),
                  L"Start with &Windows (auto-launch at logon)",
                  IDC_AUTOSTART_CHECK);
        y += px(kRowH) + px(kGap);

        add_label(x, y, label_w, px(kRowH), L"&Start delay (sec)",
                  IDC_AUTOSTART_DELAY_LABEL);
        add_edit(form_x, y, px(kPaddingFieldW), px(kRowH), IDC_AUTOSTART_DELAY_EDIT);
        add_hint(form_x + px(kPaddingFieldW) + px(kGapTight),
                 y, field_w - px(kPaddingFieldW) - px(kGapTight),
                 L"Seconds to wait before the first auto-tile after "
                 L"autostart. 0 = off.",
                 IDC_HINT_START_DELAY);
        y += px(kRowH) + px(kGap);

        add_label(x, y, label_w, px(kRowH), L"&Log level", IDC_LOGLEVEL_LABEL);
        add_combo(form_x, y, px(kLogLevelComboW),
                  IDC_LOGLEVEL_COMBO, px(kComboDropHeight));
        y += px(kRowH) + px(kGap);

        // Config file (export / import)
        add_label(x, y, label_w, px(kRowH), L"Config &file", IDC_CFGFILE_LABEL);
        add_button(form_x, y, px(kExportBtnW), px(kRowH),
                   L"Export...", IDC_EXPORT_CONFIG_BTN);
        add_button(form_x + px(kExportBtnW) + px(kBtnGap), y, px(kImportBtnW),
                   px(kRowH), L"Import...", IDC_IMPORT_CONFIG_BTN);
        y += px(kRowH) + px(kGapBeforeButton);
```

Notes for the implementer:
- The old standalone `&Processes` column label (`IDC_PROCESS_LABEL`) is **removed** — the section header replaces it. `IDC_PROCESS_LABEL` stays in the enum (unused is fine; do not renumber the enum).
- The buttons row below (`Open &config file...`, `&Apply`, `Cancel`, `E&xit AutoTerminal`) and everything after it are untouched.
- The old `y` flow computed `form_x` inline per row; `form_x`/`full_w` locals now do it once. Row-relative button positions use `form_x + …` exactly as the old `x + label_w + px(kGapTight + 2) + …` did.

- [ ] **Step 3: Build and run the full test suite**

Run: `build.bat && build\tests\autoterminal_tests.exe`
Expected: build clean; all tests PASS.

- [ ] **Step 4: UI smoke test**

Run `build\AutoTerminal.exe` (Settings shows on first run; or tray → Settings...). Check:
1. Four section headers render semibold and larger than labels.
2. Five hint lines render smaller and dimmer than labels, no clipping at 100 % DPI.
3. Bottom button row fully visible with breathing room; nothing clipped at the dialog bottom.
4. Every control still works: monitor combo, add/remove/rename process, filter, picker refresh, all four hotkey captures (click Capture → press chord → Esc cancels), autostart check, delay, log level, Export/Import, Apply round-trips to `config.toml`.
5. DPI rebuild: with the app on a secondary monitor at a different DPI (or via Settings → Display → Scale change), confirm the dialog rebuilds with headers/hints intact and no clipping.

If anything clips vertically, adjust `kDefaultHeightPx` in ±6 px steps until the bottom margin (`kBottomMargin`) is respected.

- [ ] **Step 5: Probe-script check**

```bat
powershell -ExecutionPolicy Bypass -File scripts\probe_windows.ps1
powershell -ExecutionPolicy Bypass -File scripts\probe_settings_pos.ps1
```
Expected: settings HWND found; child-control count grows by 4 headers + 5 hints (9 more than before); reported dialog rect height ≈ 812 logical px scaled by DPI.

- [ ] **Step 6: Commit**

```bash
git add src/settings_dialog.cpp
git commit -m "settings: section headers, hint lines, consistent label wording"
```

---

## Self-Review (done)

- Spec coverage: reverse-iteration raise (Task 1), 4 sections (Task 3), 5 hints (Task 3), 6 label renames (Task 3), height bump (Task 3), manual verification checklists (Tasks 1 & 3), probe smoke (Task 3). Spec's "no unit tests affected" is superseded by the plan's real-HWND z-order test, which is strictly better coverage.
- Placeholders: none — every step carries complete code.
- Type consistency: `add_section_header(int, int, int, std::wstring_view, int)` / `add_hint(int, int, int, std::wstring_view, int)` match between Task 2 and Task 3; `WindowEntry{h, 0, L"..."}` aggregate init matches `struct WindowEntry { HWND; DWORD; std::wstring; }`.
