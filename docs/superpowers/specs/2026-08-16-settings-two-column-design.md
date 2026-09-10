# Settings dialog: two-column layout + work-area fit

Date: 2026-08-16

## Problem

The single-column Settings dialog (580 × 930 logical px) exceeds the work
area of low-resolution displays: on 1366 × 768 at 125 % scale the logical
work-area budget is roughly 1093 × 576, so the dialog's 930-px height cannot
fit. The form reads as one long unstructured scroll.

## Design

### Two-column layout

Dialog grows from 580 × 930 to **880 × 560** logical px (100 % DPI
baseline). At 125 % scale that is 1100 × 700 physical — fits 1366 × 768
work area (~720 px tall).

```
+--------------------------------------------------------------+
| Display & Layout          | Processes                         |
|   Target display          |   [hint] add / filter / picker    |
|   Padding + hint          |   list + inspector + hint         |
| Hotkeys                   | Startup & General                 |
|   [hint] 4 hotkey rows    |   autostart / delay+hint / log    |
|                           |   config file                     |
| [Open config file] [Exit]            [Apply] [Cancel]        |
+--------------------------------------------------------------+
     left col 360                  right col 476 (field 360)
```

- **Left column** (x = kLeftMargin, width 360): section *Display & Layout*
  (target display, padding + hint), section *Hotkeys* (hint + 4 rows).
- **Right column** (x = left + 360 + 16 = 390, width 476): section
  *Processes* (hint, add, filter, picker, list, inspector + hint), section
  *Startup & General* (autostart, delay + hint, log level, config file).
- Column label width 110 (left column hotkey labels use the same column
  geometry: label 116 + field 238); right column keeps label 110 +
  field 360 so the picker/list rows are unchanged from the current build.
- Hints span their column's full width (not just the field) so the two-line
  wrap budget doubles; hint texts unchanged.
- Buttons row spans the full dialog width at the bottom, unchanged captions
  and order.
- Heights: left ≈ 318, right ≈ 494 → dialog 12 + 494 + 8 + 28 + 14 ≈ 560.

### Work-area fit (small-screen guarantee)

New pure function in `tile_engine.h`:

```cpp
// Fit `window` inside `work_area`: shrink to fit, then center. Pure math.
Rect fit_rect_in(Rect window, Rect work_area);
```

- On `create_main`: query the monitor under the cursor (`MonitorFromPoint`,
  MONITORF_DEFAULTTONEAREST), take `rcWork`, pass the desired 880 × 560
  rect through `fit_rect_in`, and create the window with the result. On
  small screens the dialog shrinks to the work area and centers instead of
  overflowing.
- On `WM_DPICHANGED`: run the OS-suggested rect through `fit_rect_in` for
  the window's monitor (`MonitorFromWindow`) before applying, replacing the
  current unconditional NOMOVE apply.

## Testing

- Unit (TDD): `fit_rect_in` cases in `test_tile_engine.cpp` —
  no-op when it fits, shrink + center when too wide/tall/both, negative
  (empty) work area passthrough.
- Visual: screenshot at 100 % DPI — two columns, hints wrapped, button row
  intact, nothing clipped (`scripts/screenshot_window.ps1`).
- Probe: `probe_settings_pos.ps1` reports ≈ 880 × 560 scaled by DPI;
  `probe_windows.ps1` control count unchanged (50).
- No config, matcher, or layout-math behavior changes; the 70 existing
  tests stay green.

## Out of scope

Tabbed navigation, per-column scrolling, collapsible sections, any wording
changes (the approved wording from the previous iteration stays).
