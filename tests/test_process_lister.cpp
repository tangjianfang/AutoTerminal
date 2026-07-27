#include "process_lister.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <algorithm>
#include <string>

using namespace autoterminal;

TEST(ProcessLister, EnumerateReturnsAtLeastOneEntry) {
    auto names = enumerate_running_process_names();
    // The test process itself (or another process on the host) must show up.
    ASSERT_FALSE(names.empty());
}

TEST(ProcessLister, EnumerateExcludesSelf) {
    auto names = enumerate_running_process_names();
    // The test binary is running while we query, so it must NOT be in the
    // returned list — that would clutter the picker's "Pick from running"
    // dropdown with the test runner itself.
    wchar_t path[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    ASSERT_GT(len, 0u);
    const wchar_t* base = wcsrchr(path, L'\\');
    ASSERT_NE(base, nullptr);
    std::wstring self(base + 1);
    EXPECT_EQ(std::find(names.begin(), names.end(), self), names.end())
        << "self (" << self << ") should be filtered out";
}

TEST(ProcessLister, EnumerateIsDeduped) {
    // The picker is now ordered by frequency (most instances first), not
    // strictly alphabetical, so we only assert dedup + non-empty here. The
    // ordering itself is covered deterministically by RankByFrequency_*.
    auto names = enumerate_running_process_names();
    ASSERT_FALSE(names.empty());
    for (size_t i = 1; i < names.size(); ++i) {
        EXPECT_NE(names[i - 1], names[i]) << "duplicate at index " << i;
    }
}

TEST(ProcessLister, RankByFrequencyOrdersByCountThenName) {
    std::vector<std::wstring> in = {L"b", L"a", L"b", L"a", L"a", L"c"};
    auto out = rank_by_frequency(in);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], L"a");   // 3 occurrences
    EXPECT_EQ(out[1], L"b");   // 2
    EXPECT_EQ(out[2], L"c");   // 1
}

TEST(ProcessLister, RankByFrequencyAlphabeticalTiebreak) {
    std::vector<std::wstring> in = {L"wezterm.exe", L"WindowsTerminal.exe",
                                    L"wezterm.exe", L"WindowsTerminal.exe"};
    auto out = rank_by_frequency(in);
    ASSERT_EQ(out.size(), 2u);
    // Equal counts (2 each) -> alphabetical: 'W' (0x57) < 'w' (0x77).
    EXPECT_EQ(out[0], L"WindowsTerminal.exe");
    EXPECT_EQ(out[1], L"wezterm.exe");
}

TEST(ProcessLister, RankByFrequencyEmpty) {
    std::vector<std::wstring> in;
    EXPECT_TRUE(rank_by_frequency(in).empty());
}

TEST(ProcessLister, RankByFrequencySingle) {
    std::vector<std::wstring> in = {L"svchost.exe", L"svchost.exe"};
    auto out = rank_by_frequency(in);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], L"svchost.exe");
}

TEST(ProcessLister, EnumerateNamesAreNonEmpty) {
    auto names = enumerate_running_process_names();
    for (const auto& n : names) EXPECT_FALSE(n.empty());
}