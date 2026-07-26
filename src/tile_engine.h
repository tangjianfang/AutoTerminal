#pragma once

#include <cstdint>
#include <vector>

namespace autoterminal {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    constexpr bool is_empty() const noexcept { return w <= 0 || h <= 0; }
};

struct Layout {
    int rows = 0;
    int cols = 0;
    std::vector<Rect> cells;   // size == rows*cols; unused trailing cells are zero-sized
};

// Pure function: pick the smallest m*n >= N grid whose cell aspect ratio
// is closest to the monitor's, then fill row-major.
//
// `monitor` is in virtual-screen coordinates; the returned cells are absolute.
// `padding` shrinks every cell uniformly on all four sides (0 = edge-to-edge).
Layout compute_layout(Rect monitor, int window_count, int padding = 0);

} // namespace autoterminal