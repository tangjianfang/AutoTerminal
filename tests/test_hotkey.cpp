#include "config_store.h"

#include <gtest/gtest.h>

#include <windows.h>

using namespace autoterminal;

namespace {
// Local copies of the Win32 modifier / VK constants so we don't need to
// pull in <windows.h> (which interferes with gtest's internal macros).
constexpr UINT kModControl = 0x0002;
constexpr UINT kModAlt     = 0x0001;
constexpr UINT kModShift   = 0x0004;
constexpr UINT kModWin     = 0x0008;
constexpr UINT kVK_F12     = 0x7B;
constexpr UINT kVK_SPACE   = 0x20;
constexpr UINT kVK_TAB     = 0x09;
constexpr UINT kVK_ESCAPE  = 0x1B;
constexpr UINT kVK_RETURN  = 0x0D;
constexpr UINT kVK_LEFT    = 0x25;
constexpr UINT kVK_RIGHT   = 0x27;
constexpr UINT kVK_UP      = 0x26;
constexpr UINT kVK_DOWN    = 0x28;
} // namespace

TEST(HotkeyParse, EmptyFails) {
    EXPECT_FALSE(parse_hotkey(L"").has_value());
    EXPECT_FALSE(parse_hotkey(L"Ctrl").has_value());
}

TEST(HotkeyParse, Digits) {
    auto hk = parse_hotkey(L"Alt+5");
    ASSERT_TRUE(hk.has_value());
    EXPECT_EQ(hk->vk, static_cast<UINT>('5'));
    EXPECT_NE(hk->modifiers & kModAlt, 0u);
}

TEST(HotkeyParse, AllModifiers) {
    auto hk = parse_hotkey(L"Ctrl+Alt+Shift+Win+K");
    ASSERT_TRUE(hk.has_value());
    EXPECT_EQ(hk->vk, static_cast<UINT>('K'));
    EXPECT_NE(hk->modifiers & kModControl, 0u);
    EXPECT_NE(hk->modifiers & kModAlt,     0u);
    EXPECT_NE(hk->modifiers & kModShift,   0u);
    EXPECT_NE(hk->modifiers & kModWin,     0u);
}

TEST(HotkeyParse, NamedKeys) {
    EXPECT_EQ(parse_hotkey(L"Ctrl+Space")->vk, kVK_SPACE);
    EXPECT_EQ(parse_hotkey(L"Ctrl+Tab")->vk,   kVK_TAB);
    EXPECT_EQ(parse_hotkey(L"Ctrl+Esc")->vk,   kVK_ESCAPE);
    EXPECT_EQ(parse_hotkey(L"Ctrl+Enter")->vk, kVK_RETURN);
    EXPECT_EQ(parse_hotkey(L"Ctrl+Left")->vk,  kVK_LEFT);
    EXPECT_EQ(parse_hotkey(L"Ctrl+Right")->vk, kVK_RIGHT);
    EXPECT_EQ(parse_hotkey(L"Ctrl+Up")->vk,    kVK_UP);
    EXPECT_EQ(parse_hotkey(L"Ctrl+Down")->vk,  kVK_DOWN);
}

TEST(HotkeyFormat, Letters) {
    Hotkey hk{kModControl, static_cast<UINT>('X')};
    EXPECT_EQ(format_hotkey(hk), L"Ctrl+X");
}

TEST(HotkeyFormat, FunctionKeys) {
    Hotkey hk{kModAlt, kVK_F12};
    EXPECT_EQ(format_hotkey(hk), L"Alt+F12");
}