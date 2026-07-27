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

TEST(ProcessLister, EnumerateIsSortedAndDeduped) {
    auto names = enumerate_running_process_names();
    ASSERT_FALSE(names.empty());
    for (size_t i = 1; i < names.size(); ++i) {
        EXPECT_LT(names[i - 1], names[i]) << "not sorted at index " << i;
        EXPECT_NE(names[i - 1], names[i]) << "duplicate at index " << i;
    }
}

TEST(ProcessLister, EnumerateNamesAreNonEmpty) {
    auto names = enumerate_running_process_names();
    for (const auto& n : names) EXPECT_FALSE(n.empty());
}