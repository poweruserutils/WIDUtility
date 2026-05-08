#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace wid::util {

enum class LogLevel { Debug, Info, Warn, Error };

struct LogRecord {
    LogLevel    level;
    std::wstring message;
    std::wstring source;
};

using LogSink = std::function<void(const LogRecord&)>;

class Log {
public:
    static Log& instance();

    void addSink(LogSink sink);
    void clearSinks();

    void debug(std::wstring_view msg, std::wstring_view source = L"") { emit(LogLevel::Debug, msg, source); }
    void info (std::wstring_view msg, std::wstring_view source = L"") { emit(LogLevel::Info,  msg, source); }
    void warn (std::wstring_view msg, std::wstring_view source = L"") { emit(LogLevel::Warn,  msg, source); }
    void error(std::wstring_view msg, std::wstring_view source = L"") { emit(LogLevel::Error, msg, source); }

private:
    Log() = default;
    void emit(LogLevel, std::wstring_view, std::wstring_view);

    std::vector<LogSink> sinks_;
};

} // namespace wid::util
