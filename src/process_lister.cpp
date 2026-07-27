#include "process_lister.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <string>
#include <unordered_map>
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

std::vector<std::wstring> rank_by_frequency(std::vector<std::wstring> names) {
    std::unordered_map<std::wstring, int> counts;
    for (const auto& n : names) counts[n]++;
    std::vector<std::wstring> uniq;
    uniq.reserve(counts.size());
    for (const auto& kv : counts) uniq.push_back(kv.first);
    std::sort(uniq.begin(), uniq.end(),
        [&](const std::wstring& a, const std::wstring& b) {
            const int ca = counts[a];
            const int cb = counts[b];
            if (ca != cb) return ca > cb;   // occurrence count descending
            return a < b;                    // alphabetical tiebreak
        });
    return uniq;
}

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

    // Deduplicate and order by frequency so the picker's dropdown leads with
    // the processes the user most likely wants (e.g. the terminal host that
    // has N windows open), not an alphabetical dump.
    return rank_by_frequency(std::move(names));
}

} // namespace autoterminal