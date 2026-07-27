#include "config_store.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// windows.h is needed for UINT and the MOD_* constants referenced below.
#include <windows.h>

using namespace autoterminal;

namespace {

std::atomic<int> g_counter{0};

std::filesystem::path tmp_dir() {
    auto base = std::filesystem::temp_directory_path() /
                std::filesystem::path("autoterminal-tests-" + std::to_string(g_counter.fetch_add(1)));
    std::filesystem::create_directories(base);
    return base;
}

std::filesystem::path tmp_file(const std::string& name) {
    auto p = tmp_dir() / name;
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return p;
}

} // namespace

TEST(ConfigStore, DefaultRoundTrip) {
    auto file_path = tmp_file("defaults.toml");
    auto loaded = load_config(file_path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FALSE(loaded->process_names.empty());
    EXPECT_NE(loaded->hotkey_tile.vk, 0u);
}

TEST(ConfigStore, ParsesWrittenFile) {
    auto file_path = tmp_file("roundtrip.toml");
    auto first = load_config(file_path);
    ASSERT_TRUE(first.has_value());
    auto second = load_config(file_path);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->process_names.size(), second->process_names.size());
    EXPECT_EQ(first->hotkey_tile.vk, second->hotkey_tile.vk);
}

TEST(ConfigStore, RejectsGarbage) {
    auto file_path = tmp_file("garbage.toml");
    {
        std::ofstream f(file_path);
        f << "this is = not valid [toml\n";
    }
    auto loaded = load_config(file_path);
    EXPECT_FALSE(loaded.has_value());
}

TEST(ConfigStore, MultiElementProcessNamesRoundTrip) {
    // Build a config with three distinct process names and verify the
    // TOML array round-trip preserves the order and content.
    auto file_path = tmp_file("multi.toml");
    Config cfg;
    cfg.process_names = {L"WindowsTerminal.exe", L"wezterm.exe",
                         L"powershell.exe"};
    cfg.target_monitor.clear();
    save_config(file_path, cfg);

    auto loaded = load_config(file_path);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->process_names.size(), 3u);
    EXPECT_EQ(loaded->process_names[0], L"WindowsTerminal.exe");
    EXPECT_EQ(loaded->process_names[1], L"wezterm.exe");
    EXPECT_EQ(loaded->process_names[2], L"powershell.exe");
}

TEST(ConfigStore, PreservesProcessNameCasing) {
    // The matcher lowercases internally; TOML must preserve the user's
    // casing so the picker dropdown reads "WindowsTerminal.exe" not
    // "windowsterminal.exe".
    auto file_path = tmp_file("casing.toml");
    Config cfg;
    cfg.process_names = {L"WezTerm.exe"};   // mixed case intentionally
    save_config(file_path, cfg);
    auto loaded = load_config(file_path);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->process_names.size(), 1u);
    EXPECT_EQ(loaded->process_names[0], L"WezTerm.exe");
}

TEST(ConfigStore, AutostartDelayRoundTrip) {
    auto file_path = tmp_file("delay.toml");
    Config cfg;
    cfg.autostart_delay = 15;
    save_config(file_path, cfg);
    auto loaded = load_config(file_path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->autostart_delay, 15);
}

TEST(ConfigStore, AutostartDelayClampsNegative) {
    auto file_path = tmp_file("delay_neg.toml");
    {
        std::ofstream f(file_path);
        f << "[ui]\nautostart_delay = -5\n";
    }
    auto loaded = load_config(file_path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->autostart_delay, 0);
}

TEST(ConfigStore, TileSpecificHotkeyRoundTrip) {
    auto file_path = tmp_file("tilespec.toml");
    Config cfg;
    cfg.hotkey_tile_specific = Hotkey{MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT,
                                      static_cast<UINT>('Y')};
    save_config(file_path, cfg);
    auto loaded = load_config(file_path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->hotkey_tile_specific.vk, static_cast<UINT>('Y'));
    EXPECT_NE(loaded->hotkey_tile_specific.modifiers & MOD_SHIFT, 0u);
}

TEST(ConfigStore, TileSpecificHotkeyDisabledByDefault) {
    auto file_path = tmp_file("tilespec_default.toml");
    Config cfg;  // hotkey_tile_specific defaults to {0, 0} — disabled
    save_config(file_path, cfg);
    // save_config must omit tile_specific when vk == 0; reload keeps the default.
    auto loaded = load_config(file_path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->hotkey_tile_specific.vk, 0u);
    EXPECT_EQ(loaded->hotkey_tile_specific.modifiers, 0u);
}

TEST(ConfigStore, ProcessRuleRoundTrip) {
    // A config with per-process layout + monitor bindings must round-trip via
    // the [[targets.process]] table array.
    auto file_path = tmp_file("rules.toml");
    Config cfg;
    cfg.process_names   = {L"WindowsTerminal.exe", L"wezterm.exe", L"pwsh.exe"};
    cfg.process_layouts = {LayoutMode::Grid, LayoutMode::Stack, LayoutMode::Monocle};
    cfg.process_monitors = {L"", L"primary", L"Dell U2723QE"};
    save_config(file_path, cfg);

    auto loaded = load_config(file_path);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->process_names.size(), 3u);
    EXPECT_EQ(loaded->process_names[0], L"WindowsTerminal.exe");
    EXPECT_EQ(loaded->process_names[1], L"wezterm.exe");
    EXPECT_EQ(loaded->process_names[2], L"pwsh.exe");
    // Lockstep vectors survive the round-trip.
    ASSERT_EQ(loaded->process_layouts.size(), 3u);
    EXPECT_EQ(loaded->process_layouts[0], LayoutMode::Grid);
    EXPECT_EQ(loaded->process_layouts[1], LayoutMode::Stack);
    EXPECT_EQ(loaded->process_layouts[2], LayoutMode::Monocle);
    ASSERT_EQ(loaded->process_monitors.size(), 3u);
    EXPECT_EQ(loaded->process_monitors[0], L"");
    EXPECT_EQ(loaded->process_monitors[1], L"primary");
    EXPECT_EQ(loaded->process_monitors[2], L"Dell U2723QE");
}

TEST(ConfigStore, BareArrayBackwardCompat) {
    // A legacy config that only has a bare process_names string array must
    // load into process_names with empty layout/monitor vectors (inherit Grid
    // + the global target_monitor).
    auto file_path = tmp_file("legacy.toml");
    {
        std::ofstream f(file_path);
        f << "[targets]\n"
             "process_names = [\"WindowsTerminal.exe\", \"wezterm.exe\"]\n"
             "target_monitor = \"\"\n";
    }
    auto loaded = load_config(file_path);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->process_names.size(), 2u);
    EXPECT_EQ(loaded->process_names[0], L"WindowsTerminal.exe");
    EXPECT_EQ(loaded->process_names[1], L"wezterm.exe");
    // No per-process overrides → both vectors empty so layout_for/monitor_for
    // fall back to Grid / inherit.
    EXPECT_TRUE(loaded->process_layouts.empty());
    EXPECT_TRUE(loaded->process_monitors.empty());
    EXPECT_EQ(layout_for(*loaded, 0), LayoutMode::Grid);
    EXPECT_TRUE(monitor_for(*loaded, 0).empty());
}

TEST(ConfigStore, DefaultConfigSavesBareArray) {
    // A default config (all Grid, all empty monitors) must save back as the
    // bare process_names array, byte-compatible with older installs.
    auto file_path = tmp_file("default_fmt.toml");
    Config cfg;  // single default name, no per-process overrides
    save_config(file_path, cfg);
    std::ifstream f(file_path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("process_names"), std::string::npos);
    EXPECT_EQ(content.find("[[targets.process]]"), std::string::npos);
}

TEST(ConfigStore, PerProcessConfigSavesTableArray) {
    // Once any rule uses a non-default layout/monitor, the save format
    // switches to the [[targets.process]] table array.
    auto file_path = tmp_file("rules_fmt.toml");
    Config cfg;
    cfg.process_names = {L"WindowsTerminal.exe", L"wezterm.exe"};
    cfg.process_layouts = {LayoutMode::Grid, LayoutMode::Stack};
    save_config(file_path, cfg);
    std::ifstream f(file_path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("[[targets.process]]"), std::string::npos);
}

TEST(ConfigStore, LayoutModeParseAndName) {
    EXPECT_EQ(parse_layout_mode("grid"), LayoutMode::Grid);
    EXPECT_EQ(parse_layout_mode("stack"), LayoutMode::Stack);
    EXPECT_EQ(parse_layout_mode("monocle"), LayoutMode::Monocle);
    EXPECT_EQ(parse_layout_mode("unknown"), LayoutMode::Grid);  // default
    EXPECT_EQ(layout_mode_name(LayoutMode::Grid), "grid");
    EXPECT_EQ(layout_mode_name(LayoutMode::Stack), "stack");
    EXPECT_EQ(layout_mode_name(LayoutMode::Monocle), "monocle");
}

TEST(ConfigStore, HotkeyParseBasic) {
    auto hk = parse_hotkey(L"Ctrl+Alt+T");
    ASSERT_TRUE(hk.has_value());
    EXPECT_EQ(hk->vk, static_cast<UINT>('T'));
    EXPECT_NE(hk->modifiers & 0x0002u, 0u);   // MOD_CONTROL
    EXPECT_NE(hk->modifiers & 0x0001u, 0u);   // MOD_ALT
}

TEST(ConfigStore, HotkeyParseFKey) {
    auto hk = parse_hotkey(L"Ctrl+Shift+F12");
    ASSERT_TRUE(hk.has_value());
    EXPECT_EQ(hk->vk, static_cast<UINT>(0x7B));   // VK_F12
    EXPECT_NE(hk->modifiers & 0x0004u, 0u);      // MOD_SHIFT
}

TEST(ConfigStore, HotkeyParseUnknownToken) {
    auto hk = parse_hotkey(L"Garbage+A");
    EXPECT_FALSE(hk.has_value());
}

TEST(ConfigStore, HotkeyFormatRoundTrip) {
    auto hk = parse_hotkey(L"Ctrl+Alt+T");
    ASSERT_TRUE(hk.has_value());
    EXPECT_EQ(format_hotkey(*hk), L"Ctrl+Alt+T");
}

TEST(ConfigStore, ExportImportRoundTrip) {
    // Mirrors the Settings dialog's Export (copy_file of the live config) then
    // Import (load_config of the copy) path: a config saved to file A, copied
    // verbatim to file B, must load back identically.
    auto file_a = tmp_file("export_src.toml");
    auto file_b = tmp_file("export_dst.toml");
    Config cfg;
    cfg.process_names = {L"WindowsTerminal.exe", L"wezterm.exe"};
    cfg.target_monitor = L"\\\\.\\DISPLAY2";
    cfg.padding = 12;
    cfg.autostart = true;
    cfg.log_level = LogLevel::Warn;
    cfg.hotkey_tile = Hotkey{MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                             static_cast<UINT>('T')};
    save_config(file_a, cfg);

    std::error_code ec;
    std::filesystem::copy_file(file_a, file_b,
        std::filesystem::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec) << "copy_file failed: " << ec.message();

    auto loaded = load_config(file_b);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->process_names.size(), cfg.process_names.size());
    EXPECT_EQ(loaded->process_names[0], L"WindowsTerminal.exe");
    EXPECT_EQ(loaded->process_names[1], L"wezterm.exe");
    EXPECT_EQ(loaded->target_monitor, cfg.target_monitor);
    EXPECT_EQ(loaded->padding, cfg.padding);
    EXPECT_EQ(loaded->autostart, cfg.autostart);
    EXPECT_EQ(loaded->log_level, cfg.log_level);
    EXPECT_EQ(loaded->hotkey_tile.vk, cfg.hotkey_tile.vk);
}