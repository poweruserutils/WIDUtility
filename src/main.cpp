#include "gui/MainWindow.h"
#include "util/Log.h"
#include "util/Paths.h"

#include <windows.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace {

// Convert UTF-16 to UTF-8 for binary write. We do not use wofstream:
// its default codecvt aborts the stream on any wide char outside the
// narrow locale (e.g. arrows, em-dashes), silently dropping every
// subsequent log line.
static std::string toUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                        out.data(), n, nullptr, nullptr);
    return out;
}

void installFileLogSink(const std::wstring& path) {
    auto file = std::make_shared<std::ofstream>(
        path, std::ios::app | std::ios::binary);
    if (!*file) return;

    auto mtx = std::make_shared<std::mutex>();
    *file << "---- WID Utility started ----\n";
    file->flush();

    wid::util::Log::instance().addSink(
        [file, mtx](const wid::util::LogRecord& r) {
            std::lock_guard<std::mutex> lk(*mtx);
            const char* lvl = "INF";
            switch (r.level) {
                case wid::util::LogLevel::Debug: lvl = "DBG"; break;
                case wid::util::LogLevel::Info:  lvl = "INF"; break;
                case wid::util::LogLevel::Warn:  lvl = "WRN"; break;
                case wid::util::LogLevel::Error: lvl = "ERR"; break;
            }
            SYSTEMTIME t; GetLocalTime(&t);
            char ts[32];
            sprintf_s(ts, "%02d:%02d:%02d.%03d",
                      t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
            *file << ts << " [" << lvl << "] ";
            if (!r.source.empty()) *file << toUtf8(r.source) << ": ";
            *file << toUtf8(r.message) << "\n";
            file->flush();
        });
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    wid::util::cleanOldScratchRoots();
    installFileLogSink(wid::util::currentLogFile().wstring());

    wid::util::Log::instance().info(
        L"Log file: " + wid::util::currentLogFile().wstring(), L"main");

    wid::gui::MainWindow window(hInstance);
    if (!window.create()) {
        MessageBoxW(nullptr, L"Failed to create main window.",
                    L"WID Utility", MB_ICONERROR);
        return 1;
    }
    return window.runMessageLoop();
}
