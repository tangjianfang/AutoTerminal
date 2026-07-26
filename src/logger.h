#pragma once

#include <cstdarg>
#include <string>
#include <string_view>

namespace autoterminal {

enum class LogLevel {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
};

void init_logger(const std::wstring& log_path, LogLevel min_level);
void set_log_level(LogLevel level);
LogLevel current_log_level();

namespace log_detail {
    // snprintf-style formatter for the LOG_* macros. Always returns a wide
    // string. `fmt` is ASCII-only; arguments are converted with %s/%d/etc.
    std::wstring format_msg(const char* fmt, ...);
    std::wstring vformat_msg(const char* fmt, va_list ap);
} // namespace log_detail

// Write a line to the log file (and OutputDebugString) if level >= min.
void log(LogLevel level, std::wstring_view message);

} // namespace autoterminal

#define AT_LOG_DEBUG(...) ::autoterminal::log(::autoterminal::LogLevel::Debug, ::autoterminal::log_detail::format_msg(__VA_ARGS__))
#define AT_LOG_INFO(...)  ::autoterminal::log(::autoterminal::LogLevel::Info,  ::autoterminal::log_detail::format_msg(__VA_ARGS__))
#define AT_LOG_WARN(...)  ::autoterminal::log(::autoterminal::LogLevel::Warn,  ::autoterminal::log_detail::format_msg(__VA_ARGS__))
#define AT_LOG_ERROR(...) ::autoterminal::log(::autoterminal::LogLevel::Error, ::autoterminal::log_detail::format_msg(__VA_ARGS__))