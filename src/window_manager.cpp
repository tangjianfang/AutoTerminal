#include "window_manager.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <unordered_set>

#include "logger.h"

namespace autoterminal {

namespace {

struct EnumCtx {
    std::vector<WindowEntry>* out;
    const std::unordered_set<std::wstring>* allowed;
};

bool ends_with_ci(const std::wstring& s, const std::wstring& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (std::towlower(s[s.size() - suffix.size() + i]) !=
            std::towlower(suffix[i])) return false;
    }
    return true;
}

std::wstring process_name_from_pid(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t path[MAX_PATH]{};
    DWORD len = MAX_PATH;
    if (!QueryFullProcessImageNameW(h, 0, path, &len)) {
        CloseHandle(h);
        return {};
    }
    CloseHandle(h);
    const wchar_t* base = wcsrchr(path, L'\\');
    return base ? std::wstring(base + 1) : std::wstring(path);
}

BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lparam) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lparam);

    if (!IsWindow(hwnd)) return TRUE;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (style & WS_CHILD) return TRUE;
    if (!(style & WS_VISIBLE)) return TRUE;          // skip hidden windows

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    std::wstring name = process_name_from_pid(pid);
    if (name.empty()) return TRUE;
    if (ctx->allowed->find(name) == ctx->allowed->end()) return TRUE;

    ctx->out->push_back(WindowEntry{hwnd, pid, std::move(name)});
    return TRUE;
}

} // namespace

std::vector<WindowEntry> collect_terminal_windows(
    const std::vector<std::wstring>& allowed_processes) {
    std::vector<WindowEntry> out;
    std::unordered_set<std::wstring> allowed(allowed_processes.begin(),
                                             allowed_processes.end());
    EnumCtx ctx{&out, &allowed};
    EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&ctx));
    return out;
}

int apply_layout(const std::vector<WindowEntry>& windows, const Layout& layout) {
    int placed = 0;
    int n = std::min<int>(static_cast<int>(windows.size()),
                          static_cast<int>(layout.cells.size()));
    for (int i = 0; i < n; ++i) {
        HWND h = windows[i].hwnd;
        const Rect& cell = layout.cells[i];
        if (cell.is_empty()) continue;

        // Auto-restore minimized / maximized windows before positioning.
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(h, &wp) &&
            (wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_SHOWMAXIMIZED)) {
            ShowWindow(h, SW_RESTORE);
        }

        // SWP_NOZORDER | SWP_NOACTIVATE → preserve current z-order and focus.
        if (SetWindowPos(h, nullptr,
                         cell.x, cell.y, cell.w, cell.h,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
            ++placed;
        } else {
            AT_LOG_WARN("SetWindowPos failed for hwnd=0x%p (gle=%lu)", h, GetLastError());
        }
    }
    return placed;
}

} // namespace autoterminal