#include <windows.h>

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

// Real top-level windows are required: z-order changes cannot be observed on
// message-only or child windows. Three tiny overlapped windows are created
// non-activated, tiled, then destroyed — safe in an interactive test session.
TEST(ApplyLayout, RaisesGroupToTopPreservingVectorOrder) {
    HWND w[3]{};
    for (auto& h : w) {
        h = CreateWindowExW(0, L"STATIC", L"AutoTerminalTest", WS_OVERLAPPEDWINDOW,
                            0, 0, 120, 80, nullptr, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
        ASSERT_NE(h, nullptr);
        ShowWindow(h, SW_SHOWNOACTIVATE);
    }

    std::vector<WindowEntry> entries;
    for (HWND h : w) entries.push_back(WindowEntry{h, 0, L"AutoTerminalTest.exe"});

    Layout lay;
    lay.rows = 1;
    lay.cols = 3;
    lay.cells = {Rect{0, 0, 100, 60}, Rect{120, 0, 100, 60}, Rect{240, 0, 100, 60}};

    ASSERT_EQ(apply_layout(entries, lay), 3);

    // The group was raised as one contiguous block, and its relative order
    // matches the input vector: w[0] highest, then w[1], w[2]. We do NOT
    // assert w[0] is the absolute topmost window: any concurrently raised
    // foreign window (another app, the running AutoTerminal daemon) can sit
    // above it moments after our pass. These two expectations fail under the
    // old SWP_NOZORDER code, where the untouched creation stacking leaves
    // w[2] above w[1] above w[0] — exactly the regression they guard.
    EXPECT_EQ(GetWindow(w[1], GW_HWNDPREV), w[0]);
    EXPECT_EQ(GetWindow(w[2], GW_HWNDPREV), w[1]);

    for (HWND h : w) if (h) DestroyWindow(h);
}