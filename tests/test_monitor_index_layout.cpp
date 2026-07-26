#include "monitor_index.h"

#include <gtest/gtest.h>

using namespace autoterminal;

TEST(MonitorIndex, ResolveEmptyFallsBackToPrimary) {
    std::vector<MonitorInfo> monitors = {
        {{0, 0, 1920, 1080}, L"\\\\.\\DISPLAY1", L"Dell U2723QE", true},
        {{1920, 0, 1920, 1080}, L"\\\\.\\DISPLAY2", L"LG UltraFine", false},
    };
    const MonitorInfo* m = resolve_monitor(monitors, L"");
    ASSERT_NE(m, nullptr);
    EXPECT_TRUE(m->primary);
}

TEST(MonitorIndex, ResolveByFriendlyName) {
    std::vector<MonitorInfo> monitors = {
        {{0, 0, 1920, 1080}, L"\\\\.\\DISPLAY1", L"Dell U2723QE", true},
        {{1920, 0, 1920, 1080}, L"\\\\.\\DISPLAY2", L"LG UltraFine", false},
    };
    const MonitorInfo* m = resolve_monitor(monitors, L"LG UltraFine");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->gdi_name, L"\\\\.\\DISPLAY2");
}

TEST(MonitorIndex, ResolveByGdiName) {
    std::vector<MonitorInfo> monitors = {
        {{0, 0, 1920, 1080}, L"\\\\.\\DISPLAY1", L"Dell U2723QE", true},
    };
    const MonitorInfo* m = resolve_monitor(monitors, L"\\\\.\\DISPLAY1");
    ASSERT_NE(m, nullptr);
    EXPECT_TRUE(m->primary);
}

TEST(MonitorIndex, UnknownReturnsNull) {
    std::vector<MonitorInfo> monitors = {
        {{0, 0, 1920, 1080}, L"\\\\.\\DISPLAY1", L"Dell U2723QE", true},
    };
    EXPECT_EQ(resolve_monitor(monitors, L"Nonexistent"), nullptr);
}

TEST(MonitorLogLabel, EmptyAndAscii) {
    EXPECT_EQ(monitor_log_label(L""), "(primary)");
    EXPECT_EQ(monitor_log_label(L"Dell U2723QE"), "Dell U2723QE");
}