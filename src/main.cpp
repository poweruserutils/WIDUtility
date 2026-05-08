#include "gui/MainWindow.h"
#include "util/Log.h"
#include "util/Paths.h"

#include <windows.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace {

void installFileLogSink(const std::wstring& path) {
    auto file = std::make_shared<std::wofstream>(path, std::ios::app);
    if (!*file) return;

    auto mtx = std::make_shared<std::mutex>();
    *file << L"---- WID Utility started ----\n";
    file->flush();

    wid::util::Log::instance().addSink(
        [file, mtx](const wid::util::LogRecord& r) {
            std::lock_guard<std::mutex> lk(*mtx);
            const wchar_t* lvl = L"INF";
            switch (r.level) {
                case wid::util::LogLevel::Debug: lvl = L"DBG"; break;
                case wid::util::LogLevel::Info:  lvl = L"INF"; break;
                case wid::util::LogLevel::Warn:  lvl = L"WRN"; break;
                case wid::util::LogLevel::Error: lvl = L"ERR"; break;
            }
            SYSTEMTIME t; GetLocalTime(&t);
            wchar_t ts[32];
            swprintf_s(ts, L"%02d:%02d:%02d.%03d",
                       t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
            *file << ts << L" [" << lvl << L"] ";
            if (!r.source.empty()) *file << r.source << L": ";
            *file << r.message << L"\n";
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
