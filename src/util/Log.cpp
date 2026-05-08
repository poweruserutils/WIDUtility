#include "util/Log.h"

namespace wid::util {

Log& Log::instance() {
    static Log inst;
    return inst;
}

void Log::addSink(LogSink sink) {
    sinks_.push_back(std::move(sink));
}

void Log::clearSinks() {
    sinks_.clear();
}

void Log::emit(LogLevel level, std::wstring_view msg, std::wstring_view source) {
    LogRecord rec{ level, std::wstring(msg), std::wstring(source) };
    for (const auto& s : sinks_) s(rec);
}

} // namespace wid::util
