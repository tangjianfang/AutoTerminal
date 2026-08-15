# Raise-on-tile + Settings dialog text polish

Date: 2026-08-15

## Summary

Two independent, small features:

1. **Raise-on-tile** — after every re-tile pass, raise all managed windows
   above other applications' windows (Z-order only, no focus change, not a
   permanent topmost).
2. **Settings dialog text polish** — section headers with a font hierarchy,
   secondary hint lines, and consistent label wording. English UI, unchanged.

No new config fields, no NativeFrameUI changes, no config format changes.

## 1. Raise-on-tile

### Behavior

- Trigger scope: **every** path that ends in `apply_layout` — auto-tile
  (debounced), Tile-now hotkey, Tile-specific hotkey, and the preview
  overlay's apply pass. No path is excluded.
- Z-order semantics: managed windows are raised as a group to the top of the
  Z-order (`HWND_TOP`). The window that was topmost among the group before
  the pass remains the topmost of the group after it. `SWP_NOACTIVATE` keeps
  the current focus untouched — a re-tile never steals the foreground.
- Raise-once only: `HWND_TOP`, never `HWND_TOPMOST`. Nothing becomes
  always-on-top.

### Implementation

`window_manager.cpp`, `apply_layout`:

- Iterate the placement loop in **reverse** (i = n-1 → 0).
- Change the single `SetWindowPos` call per window from
  `nullptr + SWP_NOZORDER` to `hWndInsertAfter = HWND_TOP`, keeping
  `SWP_NOACTIVATE | SWP_FRAMECHANGED`.
- Rationale for reverse iteration: `EnumWindows` returns windows in
  top-to-bottom Z-order, and successive `HWND_TOP` inserts leave the
  *last-processed* window highest. Processing in reverse preserves the
  group's internal relative order while the whole group ends above other
  applications.
- Placement correctness is unaffected by iteration order (each window's cell
  is computed independently).
- The preview overlay never moves windows, so it is unaffected.

## 2. Settings dialog text polish

All changes inside `settings_dialog.cpp`.

### Section headers

New helper `add_section_header(x, y, w, text, id)` built on `nfui::StaticText`
with `TextStyle{ use_semibold = true, font_size_pt = font_pt::md (14 pt) }`,
theme text color, height `kRowH`, preceded by a `kGapSection = 10` gap.

The form is regrouped into four sections (no rows added or removed):

| Section | Rows |
|---|---|
| Display & Layout | Display combo, Padding |
| Processes | free-text Add, Filter, running-process picker, configured list, per-row Layout/Monitor inspector |
| Hotkeys | 4 hotkey rows |
| Startup & General | autostart check, Start delay, Log level, Config file Export/Import |

### Hint lines

New helper `add_hint(x, y, w, text, id)`: `nfui::StaticText` with theme
`text_secondary` color and `font_pt::xs` (11 pt), placed directly under the
row it explains:

- Hotkeys (section lead-in): "Click Capture, then press the key combination.
  Esc cancels."
- Processes: "Matched by process name. Double-click a row to rename; drag to
  reorder."
- Layout / Monitor inspector: "Applies to the process selected above."
- Padding: "Gap around each tiled window. 0 = edge-to-edge."
- Start delay: "Seconds to wait before the first auto-tile after autostart.
  0 = off."

### Label wording

Drop the "hotkey" suffix (the section header already says it), unify on noun
phrases, keep mnemonic accelerators unique per dialog:

| Old | New |
|---|---|
| `&Display` | `Target &display` |
| `&Tile-now hotkey` | `&Tile now` |
| `P&ause hotkey` | `Pa&use / resume` |
| `Tile-&specific hotkey` | `Tile &first process` (it tiles only `process_names[0]`) |
| `Pre&view tiling hotkey` | `Pre&view tiling` |
| `Start &delay (s)` | `Start &delay (sec)` |

Unchanged: `&Padding (px)`, `&Filter running`, `La&yout`, `Mo&nitor`,
`&Log level`, `Config &file`, checkbox and button captions.

### Sizing

`kDefaultHeightPx` 650 → ~850 logical px (4 headers, 5 hints, section gaps);
width stays 580. `rebuild_layout` destroys and recreates all children on DPI
change, so the new helpers are DPI-safe by construction.

## Testing & verification

- Unit tests: none affected — no config, matcher, or layout-math changes.
  Z-order behavior needs real HWNDs and is not unit-testable here.
- Manual verification checklist (raise-on-tile):
  1. Open 3 terminals + 1 unrelated app window covering them.
  2. Trigger a re-tile (hotkey, new terminal, close a terminal).
  3. Expect: terminal group ends above the unrelated app; the focused
     non-terminal window keeps focus; the group's internal order is
     unchanged; nothing becomes always-on-top afterwards.
- UI smoke: `probe_windows.ps1` (control counts), `probe_settings_pos.ps1`
  (dialog rect at current DPI), visual check at 100 % and 150 % DPI.
