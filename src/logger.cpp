#include "logger.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>

#include <windows.h>

namespace autoterminal {

namespace {

std::wstring g_log_path;
std::atomic<LogLevel> g_min_level{LogLevel::Info};
std::mutex g_file_mutex;

const wchar_t* level_tag(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Debug: return L"DBG";
        case LogLevel::Info:  return L"INF";
        case LogLevel::Warn:  return L"WRN";
        case LogLevel::Error: return L"ERR";
    }
    return L"???";
}

std::wstring now_string() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t   = system_clock::to_time_t(now);
    auto ms  = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    localtime_s(&tm, &t);
    std::wostringstream oss;
    oss << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S")
        << L'.' << std::setw(3) << std::setfill(L'0') << ms;
    return oss.str();
}

std::wstring widen(const char* s) {
    if (!s) return {};
    std::wstring out;
    while (*s) out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s++)));
    return out;
}

} // namespace

void init_logger(const std::wstring& log_path, LogLevel min_level) {
    g_log_path = log_path;
    g_min_level.store(min_level);
}

void set_log_level(LogLevel level) { g_min_level.store(level); }
LogLevel current_log_level() { return g_min_level.load(); }

namespace log_detail {

std::wstring vformat_msg(const char* fmt, va_list ap) {
    if (!fmt) return {};
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    if (needed < 0) return widen(fmt);
    std::string buf(static_cast<size_t>(needed) + 1, '\0');
    vsnprintf(buf.data(), buf.size(), fmt, ap);
    buf.pop_back();
    std::wstring out;
    out.reserve(buf.size());
    for (char c : buf) out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return out;
}

std::wstring format_msg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto out = vformat_msg(fmt, ap);
    va_end(ap);
    return out;
}

} // namespace log_detail

void log(LogLevel level, std::wstring_view message) {
    if (static_cast<int>(level) < static_cast<int>(g_min_level.load())) return;

    std::wostringstream line;
    line << L'[' << now_string() << L"] [" << level_tag(level) << L"] " << message << L'\n';
    std::wstring formatted = line.str();

    OutputDebugStringW(formatted.c_str());

    if (g_log_path.empty()) return;
    std::lock_guard<std::mutex> lock(g_file_mutex);
    FILE* fp = nullptr;
    _wfopen_s(&fp, g_log_path.c_str(), L"a, ccs=UTF-8");
    if (!fp) return;
    fputws(formatted.c_str(), fp);
    fclose(fp);
}

} // namespace autoterminal