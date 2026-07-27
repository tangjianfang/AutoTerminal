#pragma once

#include <string>
#include <vector>

namespace autoterminal {

// Returns the basenames (e.g. "WindowsTerminal.exe") of currently-running
// processes on the system, deduplicated and sorted alphabetically. Excludes
// the AutoTerminal executable itself so the picker doesn't surface its own
// image. Snapshot via CreateToolhelp32Snapshot — kernel32 only, no extra
// link flags beyond what AutoTerminal already uses.
//
// Note: we are a 64-bit process (PE32+); CreateToolhelp32Snapshot from a
// 32-bit process can't enumerate 64-bit processes, but that's not a
// concern here.
std::vector<std::wstring> enumerate_running_process_names() noexcept;

} // namespace autoterminal