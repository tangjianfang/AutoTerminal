#include "config_store.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>

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