#pragma once

#include <string>
#include <vector>

namespace autoterminal {

// Deduplicate `names` (which may contain repeats — e.g. several msbuild.exe
// workers share an image name) and sort by occurrence count descending, with
// alphabetical order breaking ties. Pure function exposed for unit testing.
std::vector<std::wstring> rank_by_frequency(std::vector<std::wstring> names);

// Returns the basenames (e.g. "WindowsTerminal.exe") of currently-running
// processes on the system, deduplicated and ordered by frequency (most
// instances first, ties alphabetical) so the picker surfaces the processes
// the user most likely wants at the top. Excludes the AutoTerminal executable
// itself so the picker doesn't surface its own image. Snapshot via
// CreateToolhelp32Snapshot — kernel32 only, no extra link flags beyond what
// AutoTerminal already uses.
//
// Note: we are a 64-bit process (PE32+); CreateToolhelp32Snapshot from a
// 32-bit process can't enumerate 64-bit processes, but that's not a concern.
std::vector<std::wstring> enumerate_running_process_names() noexcept;

} // namespace autoterminal