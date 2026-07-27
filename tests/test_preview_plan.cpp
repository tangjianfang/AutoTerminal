#include "config_store.h"
#include "monitor_index.h"
#include "tile_engine.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <windows.h>

using namespace autoterminal;

namespace {

MonitorInfo make_mon(int x, int y, int w, int h, std::wstring friendly,
                     std::wstring gdi, bool primary) {
    MonitorInfo m;
    m.rect = {x, y, w, h};
    m.friendly_name = std::move(friendly);
    m.gdi_name = std::move(gdi);
    m.primary = primary;
    return m;
}

std::vector<MonitorInfo> two_monitors() {
    return {
        make_mon(0, 0, 1920, 1080, L"Dell U2723QE", L"\\\\.\\DISPLAY1", true),
        make_mon(1920, 0, 2560, 1440, L"LG 27GL850", L"\\\\.\\DISPLAY2", false),
    };
}

// Filesystem helper for the hotkey_preview round-trip test.
std::filesystem::path tmp_file(const std::string& name) {
    static int counter = 0;
    auto base = std::filesystem::temp_directory_path() /
                std::filesystem::path("autoterminal-preview-tests-" +
                                      std::to_string(counter++));
    std::filesystem::create_directories(base);
    auto p = base / name;
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return p;
}

} // namespace

TEST(PreviewPlan, OutOfRangeIndexYieldsNullMonitor) {
    Config cfg;
    cfg.process_names = {L"WindowsTerminal.exe"};
    auto monitors = two_monitors();
    auto plan = plan_layouts(cfg, monitors, {5}, [](size_t) { return 3; });
    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(plan[0].monitor, nullptr);
    EXPECT_TRUE(plan[0].name.empty());
    EXPECT_TRUE(plan[0].layout.cells.empty());
}

TEST(PreviewPlan, EmptyMonitorsYieldsAllNull) {
    Config cfg;
    cfg.process_names = {L"WindowsTerminal.exe", L"wezterm.exe"};
    std::vector<MonitorInfo> monitors;
    auto plan = plan_layouts(cfg, monitors, {0, 1}, [](size_t) { return 2; });
    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(plan[0].monitor, nullptr);
    EXPECT_EQ(plan[1].monitor, nullptr);
    EXPECT_TRUE(plan[0].layout.cells.empty());
}

TEST(PreviewPlan, PerRuleMonitorOverride) {
    Config cfg;
    cfg.process_names = {L"a.exe", L"b.exe"};
    cfg.process_monitors = {L"", L"LG 27GL850"};
    auto monitors = two_monitors();
    auto plan = plan_layouts(cfg, monitors, {0, 1}, [](size_t) { return 1; });
    ASSERT_EQ(plan.size(), 2u);
    // Rule 0 inherits target_monitor (empty) → primary Dell.
    ASSERT_NE(plan[0].monitor, nullptr);
    EXPECT_EQ(plan[0].monitor->friendly_name, L"Dell U2723QE");
    EXPECT_FALSE(plan[0].monitor_fell_back);
    // Rule 1 explicitly bound to the LG monitor.
    ASSERT_NE(plan[1].monitor, nullptr);
    EXPECT_EQ(plan[1].monitor->friendly_name, L"LG 27GL850");
    EXPECT_FALSE(plan[1].monitor_fell_back);
    EXPECT_EQ(plan[1].requested_monitor_id, L"LG 27GL850");
}

TEST(PreviewPlan, EmptyIdEmptyTargetResolvesPrimary) {
    Config cfg;
    cfg.process_names = {L"a.exe"};
    cfg.target_monitor.clear();   // inherit → primary
    auto monitors = two_monitors();
    auto plan = plan_layouts(cfg, monitors, {0}, [](size_t) { return 1; });
    ASSERT_EQ(plan.size(), 1u);
    ASSERT_NE(plan[0].monitor, nullptr);
    EXPECT_TRUE(plan[0].monitor->primary);
    EXPECT_FALSE(plan[0].monitor_fell_back);
}

TEST(PreviewPlan, UnknownIdFallsBackToPrimary) {
    Config cfg;
    cfg.process_names = {L"a.exe"};
    cfg.target_monitor = L"No Such Monitor";
    auto monitors = two_monitors();
    auto plan = plan_layouts(cfg, monitors, {0}, [](size_t) { return 1; });
    ASSERT_EQ(plan.size(), 1u);
    ASSERT_NE(plan[0].monitor, nullptr);
    EXPECT_TRUE(plan[0].monitor->primary);
    EXPECT_TRUE(plan[0].monitor_fell_back);
    EXPECT_EQ(plan[0].requested_monitor_id, L"No Such Monitor");
}

TEST(PreviewPlan, ZeroWindowsYieldsEmptyCells) {
    Config cfg;
    cfg.process_names = {L"a.exe"};
    auto monitors = two_monitors();
    auto plan = plan_layouts(cfg, monitors, {0}, [](size_t) { return 0; });
    ASSERT_EQ(plan.size(), 1u);
    ASSERT_NE(plan[0].monitor, nullptr);
    EXPECT_EQ(plan[0].window_count, 0);
    EXPECT_TRUE(plan[0].layout.cells.empty());
}

TEST(PreviewPlan, LayoutMatchesComputeLayout) {
    // For every mode and a spread of window counts, plan_layouts' cells must
    // equal compute_layout exactly (same rows/cols and per-cell geometry).
    Rect mon{0, 0, 1920, 1080};
    Config cfg;
    cfg.process_names = {L"a.exe"};
    cfg.padding = 0;
    auto monitors = {make_mon(0, 0, 1920, 1080, L"P", L"\\\\.\\DISPLAY1", true)};

    const LayoutMode modes[] = {LayoutMode::Grid, LayoutMode::Stack,
                                LayoutMode::Monocle};
    const int counts[] = {1, 2, 3, 5, 9};
    for (LayoutMode mode : modes) {
        cfg.process_layouts = {mode};
        const int mode_i = static_cast<int>(mode);
        for (int n : counts) {
            auto plan = plan_layouts(cfg, monitors, {0},
                                     [n](size_t) { return n; });
            ASSERT_EQ(plan.size(), 1u);
            Layout ref = compute_layout(mon, n, 0, mode);
            const Layout& got = plan[0].layout;
            EXPECT_EQ(got.rows, ref.rows) << "mode=" << mode_i << " n=" << n;
            EXPECT_EQ(got.cols, ref.cols) << "mode=" << mode_i << " n=" << n;
            ASSERT_EQ(got.cells.size(), ref.cells.size());
            for (size_t i = 0; i < ref.cells.size(); ++i) {
                EXPECT_EQ(got.cells[i].x, ref.cells[i].x) << "mode=" << mode_i << " n=" << n << " i=" << i;
                EXPECT_EQ(got.cells[i].y, ref.cells[i].y) << "mode=" << mode_i << " n=" << n << " i=" << i;
                EXPECT_EQ(got.cells[i].w, ref.cells[i].w) << "mode=" << mode_i << " n=" << n << " i=" << i;
                EXPECT_EQ(got.cells[i].h, ref.cells[i].h) << "mode=" << mode_i << " n=" << n << " i=" << i;
            }
        }
    }
}

TEST(PreviewPlan, PaddingPassesThrough) {
    Rect mon{0, 0, 1920, 1080};
    Config cfg;
    cfg.process_names = {L"a.exe"};
    cfg.padding = 24;
    auto monitors = {make_mon(0, 0, 1920, 1080, L"P", L"\\\\.\\DISPLAY1", true)};
    auto plan = plan_layouts(cfg, monitors, {0}, [](size_t) { return 4; });
    ASSERT_EQ(plan.size(), 1u);
    Layout ref = compute_layout(mon, 4, 24, LayoutMode::Grid);
    EXPECT_EQ(plan[0].layout.cells.size(), ref.cells.size());
    for (size_t i = 0; i < ref.cells.size(); ++i) {
        EXPECT_EQ(plan[0].layout.cells[i].x, ref.cells[i].x) << "i=" << i;
        EXPECT_EQ(plan[0].layout.cells[i].y, ref.cells[i].y) << "i=" << i;
        EXPECT_EQ(plan[0].layout.cells[i].w, ref.cells[i].w) << "i=" << i;
        EXPECT_EQ(plan[0].layout.cells[i].h, ref.cells[i].h) << "i=" << i;
    }
}

TEST(PreviewPlan, EchoesRuleIndexNameMode) {
    Config cfg;
    cfg.process_names = {L"alpha.exe", L"beta.exe"};
    cfg.process_layouts = {LayoutMode::Stack, LayoutMode::Monocle};
    auto monitors = two_monitors();
    auto plan = plan_layouts(cfg, monitors, {1}, [](size_t) { return 2; });
    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(plan[0].rule_index, 1u);
    EXPECT_EQ(plan[0].name, L"beta.exe");
    EXPECT_EQ(plan[0].mode, LayoutMode::Monocle);
    EXPECT_EQ(plan[0].window_count, 2);
}

TEST(PreviewPlan, CountFnInvokedOncePerIndex) {
    Config cfg;
    cfg.process_names = {L"a.exe", L"b.exe", L"c.exe"};
    auto monitors = two_monitors();
    int calls = 0;
    auto plan = plan_layouts(cfg, monitors, {0, 1, 2},
                             [&calls](size_t) { ++calls; return 1; });
    EXPECT_EQ(plan.size(), 3u);
    EXPECT_EQ(calls, 3);
}

TEST(PreviewPlan, HotkeyPreviewRoundTrip) {
    auto file_path = tmp_file("preview_hk.toml");
    Config cfg;
    cfg.hotkey_preview = Hotkey{MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT,
                                static_cast<UINT>('V')};
    save_config(file_path, cfg);
    auto loaded = load_config(file_path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->hotkey_preview.vk, static_cast<UINT>('V'));
    EXPECT_NE(loaded->hotkey_preview.modifiers & MOD_CONTROL, 0u);
    EXPECT_NE(loaded->hotkey_preview.modifiers & MOD_SHIFT, 0u);
}

TEST(PreviewPlan, HotkeyPreviewDisabledByDefault) {
    auto file_path = tmp_file("preview_hk_default.toml");
    Config cfg;  // hotkey_preview defaults to {0,0} — disabled / optional
    save_config(file_path, cfg);
    auto loaded = load_config(file_path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->hotkey_preview.vk, 0u);
    EXPECT_EQ(loaded->hotkey_preview.modifiers, 0u);

    // A disabled preview hotkey must be OMITTED from the file (matches the
    // tile_specific pattern), not serialized as an empty string.
    std::ifstream in(file_path, std::ios::binary);
    ASSERT_TRUE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content.find("preview"), std::string::npos)
        << "disabled preview hotkey should not be written to config; got:\n"
        << content;
}