#include "tile_engine.h"

#include <gtest/gtest.h>

using namespace autoterminal;

TEST(TileEngine, NoWindows) {
    Layout l = compute_layout({0, 0, 1920, 1080}, 0, 0);
    EXPECT_EQ(l.rows, 0);
    EXPECT_EQ(l.cols, 0);
    EXPECT_TRUE(l.cells.empty());
}

TEST(TileEngine, SingleWindowFillsMonitor) {
    Layout l = compute_layout({0, 0, 1920, 1080}, 1, 0);
    EXPECT_EQ(l.rows, 1);
    EXPECT_EQ(l.cols, 1);
    ASSERT_EQ(l.cells.size(), 1u);
    EXPECT_EQ(l.cells[0].x, 0);
    EXPECT_EQ(l.cells[0].y, 0);
    EXPECT_EQ(l.cells[0].w, 1920);
    EXPECT_EQ(l.cells[0].h, 1080);
}

TEST(TileEngine, TwoWindowsHalfAndHalf) {
    Layout l = compute_layout({0, 0, 1920, 1080}, 2, 0);
    EXPECT_GE(l.rows * l.cols, 2);
    ASSERT_EQ(l.cells.size(), static_cast<size_t>(l.rows * l.cols));
    int total_area = 0;
    for (auto& c : l.cells) total_area += c.w * c.h;
    EXPECT_EQ(total_area, 1920 * 1080);
}

TEST(TileEngine, ThreeWindows) {
    // 3 terminals → 2x2 matrix: "2 on top, 1 on bottom, 1 empty"
    Layout l = compute_layout({0, 0, 1920, 1080}, 3, 0);
    EXPECT_EQ(l.rows, 2);
    EXPECT_EQ(l.cols, 2);
    EXPECT_EQ(l.rows * l.cols, 4);   // 4 cells, 3 filled + 1 empty
    int total_area = 0;
    for (auto& c : l.cells) total_area += c.w * c.h;
    EXPECT_EQ(total_area, 1920 * 1080);
}

TEST(TileEngine, FiveWindows) {
    // 5 terminals → 2x3 matrix: "3 on top, 2 on bottom, 1 empty"
    Layout l = compute_layout({0, 0, 1920, 1080}, 5, 0);
    EXPECT_EQ(l.rows, 2);
    EXPECT_EQ(l.cols, 3);
    EXPECT_EQ(l.rows * l.cols, 6);   // 6 cells, 5 filled + 1 empty
    int total_area = 0;
    for (auto& c : l.cells) total_area += c.w * c.h;
    EXPECT_EQ(total_area, 1920 * 1080);
}

TEST(TileEngine, EightWindows) {
    // 8 terminals → 3x3 matrix (9 cells, 1 empty). The matrix-first rule
    // prefers a balanced 3x3 over a tight 2x4 strip — even though the strip
    // has slack 0 — so the user never sees single-row "column" layouts.
    Layout l = compute_layout({0, 0, 1920, 1080}, 8, 0);
    EXPECT_EQ(l.rows, 3);
    EXPECT_EQ(l.cols, 3);
    EXPECT_EQ(l.rows * l.cols, 9);
    int total_area = 0;
    for (auto& c : l.cells) total_area += c.w * c.h;
    EXPECT_EQ(total_area, 1920 * 1080);
}

TEST(TileEngine, NineWindows) {
    Layout l = compute_layout({0, 0, 1920, 1080}, 9, 0);
    EXPECT_EQ(l.rows, 3);
    EXPECT_EQ(l.cols, 3);
    int total_area = 0;
    for (auto& c : l.cells) total_area += c.w * c.h;
    EXPECT_EQ(total_area, 1920 * 1080);
}

TEST(TileEngine, TwentyWindows) {
    Layout l = compute_layout({0, 0, 3840, 2160}, 20, 0);
    EXPECT_GE(l.rows * l.cols, 20);
    int total_area = 0;
    for (auto& c : l.cells) total_area += c.w * c.h;
    EXPECT_EQ(total_area, 3840 * 2160);
}

TEST(TileEngine, UlWideMonitorPrefersMoreColumns) {
    Layout l = compute_layout({0, 0, 3440, 1440}, 6, 0);
    EXPECT_GE(l.cols, l.rows);        // ultrawide → wider grid
}

TEST(TileEngine, OffOriginMonitor) {
    // Second monitor offset to the right
    Layout l = compute_layout({1920, 0, 1920, 1080}, 4, 0);
    ASSERT_GE(l.cells.size(), 4u);
    EXPECT_EQ(l.cells[0].x, 1920);    // first cell anchored on the second monitor
}

TEST(TileEngine, PaddingShrinksCells) {
    Layout no_pad = compute_layout({0, 0, 1000, 1000}, 4, 0);
    Layout padded = compute_layout({0, 0, 1000, 1000}, 4, 10);
    for (size_t i = 0; i < padded.cells.size(); ++i) {
        EXPECT_LT(padded.cells[i].w, no_pad.cells[i].w);
        EXPECT_LT(padded.cells[i].h, no_pad.cells[i].h);
    }
}

TEST(TileEngine, TotalAreaMatchesMonitor) {
    for (int n = 1; n <= 16; ++n) {
        Layout l = compute_layout({0, 0, 1920, 1080}, n, 0);
        long long total = 0;
        for (auto& c : l.cells) total += static_cast<long long>(c.w) * c.h;
        EXPECT_EQ(total, 1920LL * 1080LL) << "n=" << n;
    }
}

TEST(TileEngine, ReminderPixelsDistributed) {
    // For 6 cells in a 1000x1000 square monitor with aspect-aware selection,
    // the algorithm picks 2 rows × 3 cols (cell aspect 3/2 ≈ 1.5 is closer to
    // monitor aspect 1.0 than 3/2 = 1.5 vs 2/3 = 0.667 — actually equal log
    // distance, so the first match wins). 1000/3 = 333 with 1 px remainder,
    // which lands on the leading cell.
    Layout l = compute_layout({0, 0, 1000, 1000}, 6, 0);
    ASSERT_GE(l.cells.size(), 6u);
    EXPECT_EQ(l.cols, 3);
    EXPECT_EQ(l.cells[0].w, 334);
    EXPECT_EQ(l.cells[1].w, 333);
    EXPECT_EQ(l.cells[2].w, 333);
}

TEST(TileEngine, GridIsTheDefaultMode) {
    // The 3-arg overload must match a 4-arg Grid call exactly.
    Layout a = compute_layout({0, 0, 1920, 1080}, 5, 0);
    Layout b = compute_layout({0, 0, 1920, 1080}, 5, 0, LayoutMode::Grid);
    EXPECT_EQ(a.rows, b.rows);
    EXPECT_EQ(a.cols, b.cols);
    ASSERT_EQ(a.cells.size(), b.cells.size());
    for (size_t i = 0; i < a.cells.size(); ++i) {
        EXPECT_EQ(a.cells[i].x, b.cells[i].x);
        EXPECT_EQ(a.cells[i].w, b.cells[i].w);
    }
}

TEST(TileEngine, StackLayoutIsSingleColumn) {
    // Stack = 1 column, N equal-height rows; total area still covers the
    // monitor exactly (remainder distributed to the leading rows).
    Layout l = compute_layout({0, 0, 1920, 1080}, 3, 0, LayoutMode::Stack);
    EXPECT_EQ(l.rows, 3);
    EXPECT_EQ(l.cols, 1);
    ASSERT_EQ(l.cells.size(), 3u);
    for (const auto& c : l.cells) EXPECT_EQ(c.w, 1920);   // full width each
    long long total = 0;
    for (const auto& c : l.cells) total += static_cast<long long>(c.w) * c.h;
    EXPECT_EQ(total, 1920LL * 1080LL);
    // Stacked top-to-bottom, anchored at the monitor origin.
    EXPECT_EQ(l.cells[0].x, 0);
    EXPECT_EQ(l.cells[0].y, 0);
    EXPECT_EQ(l.cells[2].y + l.cells[2].h, 1080);
}

TEST(TileEngine, MonocleLayoutAllFullscreen) {
    // Monocle = every window gets the full monitor rect (overlapping). The
    // cells are identical and equal to the whole monitor.
    Rect mon{0, 0, 1920, 1080};
    Layout l = compute_layout(mon, 4, 0, LayoutMode::Monocle);
    EXPECT_EQ(l.cells.size(), 4u);
    for (const auto& c : l.cells) {
        EXPECT_EQ(c.x, mon.x);
        EXPECT_EQ(c.y, mon.y);
        EXPECT_EQ(c.w, mon.w);
        EXPECT_EQ(c.h, mon.h);
    }
}

TEST(TileEngine, MonocleLayoutAppliesPadding) {
    Rect mon{10, 20, 1920, 1080};
    Layout l = compute_layout(mon, 2, 8, LayoutMode::Monocle);
    ASSERT_EQ(l.cells.size(), 2u);
    for (const auto& c : l.cells) {
        EXPECT_EQ(c.x, mon.x + 8);
        EXPECT_EQ(c.y, mon.y + 8);
        EXPECT_EQ(c.w, mon.w - 16);
        EXPECT_EQ(c.h, mon.h - 16);
    }
}