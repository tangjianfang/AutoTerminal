#include "tile_engine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace autoterminal {

namespace {

int ceil_div(int a, int b) { return (a + b - 1) / b; }

} // namespace

Layout compute_layout(Rect monitor, int window_count, int padding) {
    Layout out;
    if (window_count <= 0 || monitor.w <= 0 || monitor.h <= 0) return out;

    if (window_count == 1) {
        out.rows = 1; out.cols = 1;
        out.cells.assign(1, Rect{monitor.x, monitor.y, monitor.w, monitor.h});
        return out;
    }

    double aspect = static_cast<double>(monitor.w) / static_cast<double>(monitor.h);

    // Choose (rows, cols) with rows*cols >= N.
    //   1. minimise slack (rows*cols - N)
    //   2. among equal slack, pick cell aspect closest to monitor aspect
    //   3. tie-break: prefer more columns (rows <= cols)
    int best_rows = 1, best_cols = window_count;
    int   best_slack = window_count;
    double best_diff = std::numeric_limits<double>::infinity();
    for (int r = 1; r <= window_count; ++r) {
        int c = ceil_div(window_count, r);
        int slack = r * c - window_count;
        double cell_aspect = aspect * static_cast<double>(r) / static_cast<double>(c);
        double diff = std::abs(std::log(cell_aspect / aspect));
        bool prefer_more_cols = (c > r) && (best_cols >= best_rows);

        bool better = false;
        if (slack < best_slack) better = true;
        else if (slack == best_slack) {
            if (diff < best_diff - 1e-9) better = true;
            else if (std::abs(diff - best_diff) < 1e-9 && prefer_more_cols) better = true;
        }
        if (better) {
            best_slack = slack;
            best_diff  = diff;
            best_rows  = r;
            best_cols  = c;
        }
    }

    int cell_w = monitor.w / best_cols;
    int cell_h = monitor.h / best_rows;
    int rem_w = monitor.w - cell_w * best_cols;
    int rem_h = monitor.h - cell_h * best_rows;

    out.rows = best_rows;
    out.cols = best_cols;
    out.cells.assign(static_cast<size_t>(best_rows) * best_cols, Rect{});

    for (int r = 0; r < best_rows; ++r) {
        int y = monitor.y + r * cell_h + std::min(r, rem_h);
        int h = cell_h + (r < rem_h ? 1 : 0);
        for (int c = 0; c < best_cols; ++c) {
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
            out.cells[static_cast<size_t>(r) * best_cols + c] = cell;
        }
    }
    return out;
}

} // namespace autoterminal