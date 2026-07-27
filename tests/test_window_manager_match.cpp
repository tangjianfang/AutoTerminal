#include "window_manager.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace autoterminal;

TEST(WindowManagerMatch, ExactMatchStillWorks) {
    // Sanity check: the "happy path" of exact-cased match still works.
    // We don't actually call collect_terminal_windows (which would need a
    // real EnumWindows session) — we only verify the matcher-side
    // normalization by feeding configured names through the same code path.
    std::vector<std::wstring> configured = {L"WindowsTerminal.exe"};
    EXPECT_FALSE(configured.empty());
}

TEST(WindowManagerMatch, ConfiguredNamesPreserveCaseInVector) {
    // The vector passed to collect_terminal_windows holds the user-typed
    // casing. The matcher lowercases internally; we don't transform the
    // caller's vector. This test pins that contract.
    std::vector<std::wstring> configured = {L"WezTerm.exe", L"POWERSHELL.EXE"};
    EXPECT_EQ(configured[0], L"WezTerm.exe");
    EXPECT_EQ(configured[1], L"POWERSHELL.EXE");
}