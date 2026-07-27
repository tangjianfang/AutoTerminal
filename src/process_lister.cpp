#include "process_lister.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <string>
#include <vector>

namespace autoterminal {

namespace {

std::wstring self_basename() noexcept {
    wchar_t path[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    const wchar_t* base = wcsrchr(path, L'\\');
    return base ? std::wstring(base + 1) : std::wstring(path);
}

} // namespace

std::vector<std::wstring> enumerate_running_process_names() noexcept {
    std::vector<std::wstring> names;
    const std::wstring self = self_basename();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return names;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            // th32ProcessID == 0 is the System Idle Process; skip to avoid
            // a System Idle "entry" confusing the picker.
            if (pe.th32ProcessID == 0) continue;
            std::wstring name(pe.szExeFile);
            if (name.empty()) continue;
            if (name == self) continue;            // hide AutoTerminal itself
            names.push_back(std::move(name));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    // Deduplicate (multiple processes can share an image name — e.g. several
    // msbuild.exe workers) and sort so the picker's dropdown is stable.
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

} // namespace autoterminal