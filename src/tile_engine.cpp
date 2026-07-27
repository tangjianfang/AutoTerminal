#include "tile_engine.h"

#include <algorithm>

namespace autoterminal {

namespace {

// Choose (rows, cols) so that:
//   * rows*cols >= window_count,
//   * grid is "matrix-like" — never a 1xN strip, never an Nx1 column,
//   * columns never exceed 3 (until n >= 10, then we stack 3-wide rows).
//
// This matches the design contract:
//   n = 1       → 1x1     (one full-screen cell)
//   n = 2       → 1x2     (side-by-side)
//   n = 3..4    → 2x2     (e.g. 3 → "2 on top, 1 on bottom, 1 empty")
//   n = 5..6    → 2x3     (e.g. 5 → "3 on top, 2 on bottom, 1 empty")
//   n = 7..9    → 3x3     (full 3x3 matrix)
//   n >= 10     → 3 × ceil(n/3)   (3-wide, more rows as needed)
//
// The fill order is left-to-right, top-to-bottom, so the empty cells (if any)
// land at the end of the last row — exactly the "blank cell" the user expects.
void pick_grid(int window_count, int& rows, int& cols) {
    if      (window_count <= 1) { rows = 1; cols = 1; }
    else if (window_count == 2) { rows = 1; cols = 2; }
    else if (window_count <= 4) { rows = 2; cols = 2; }
    else if (window_count <= 6) { rows = 2; cols = 3; }
    else if (window_count <= 9) { rows = 3; cols = 3; }
    else                        { cols = 3; rows = (window_count + 2) / 3; }
}

// Split `monitor` into a rows x cols grid of absolute cells, distributing the
// remainder pixels to the first rows/cols so the grid always exactly covers
// the monitor. `padding` insets each cell uniformly on all four sides.
std::vector<Rect> grid_cells(Rect monitor, int rows, int cols, int padding) {
    std::vector<Rect> cells(static_cast<size_t>(rows) * cols, Rect{});
    int cell_w = monitor.w / cols;
    int cell_h = monitor.h / rows;
    int rem_w  = monitor.w - cell_w * cols;
    int rem_h  = monitor.h - cell_h * rows;
    for (int r = 0; r < rows; ++r) {
        int y = monitor.y + r * cell_h + std::min(r, rem_h);
        int h = cell_h + (r < rem_h ? 1 : 0);
        for (int c = 0; c < cols; ++c) {
            int x = monitor.x + c * cell_w + std::min(c, rem_w);
            int w = cell_w + (c < rem_w ? 1 : 0);
            Rect cell{x, y, w, h};
            if (padding > 0) {
                cell.x += padding;
                cell.y += padding;
                cell.w -= 2 * padding;
                cell.h -= 2 * padding;
                if (cell.w < 1) cell.w = 1;
                if (cell.h < 1) cell.h = 1;
            }
            cells[static_cast<size_t>(r) * cols + c] = cell;
        }
    }
    return cells;
}

} // namespace

Layout compute_layout(Rect monitor, int window_count, int padding) {
    return compute_layout(monitor, window_count, padding, LayoutMode::Grid);
}

Layout compute_layout(Rect monitor, int window_count, int padding, LayoutMode mode) {
    Layout out;
    if (window_count <= 0 || monitor.w <= 0 || monitor.h <= 0) return out;

    int rows = 0, cols = 0;
    switch (mode) {
        case LayoutMode::Grid:
            pick_grid(window_count, rows, cols);
            out.rows = rows;
            out.cols = cols;
            out.cells = grid_cells(monitor, rows, cols, padding);
            return out;
        case LayoutMode::Stack:
            // Single column, N equal-height rows. rows*cols == N exactly.
            rows = window_count;
            cols = 1;
            out.rows = rows;
            out.cols = cols;
            out.cells = grid_cells(monitor, rows, cols, padding);
            return out;
        case LayoutMode::Monocle: {
            // Every window fills the whole monitor (overlapping). We expose
            // rows == N, cols == 1 so cells.size() == rows*cols holds, but
            // every cell is the full monitor rect — the windows stack on top
            // of each other and the user cycles via the taskbar / Alt+Tab.
            rows = window_count;
            cols = 1;
            Rect full = monitor;
            if (padding > 0) {
                full.x += padding;
                full.y += padding;
                full.w -= 2 * padding;
                full.h -= 2 * padding;
                if (full.w < 1) full.w = 1;
                if (full.h < 1) full.h = 1;
            }
            out.rows = rows;
            out.cols = cols;
            out.cells.assign(static_cast<size_t>(window_count), full);
            return out;
        }
    }
    return out;
}

} // namespace autoterminal