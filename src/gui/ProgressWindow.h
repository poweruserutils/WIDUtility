#pragma once

#include <windows.h>
#include <atomic>
#include <string>

namespace wid::gui {

class ProgressWindow {
public:
    explicit ProgressWindow(HWND owner);
    ~ProgressWindow();

    ProgressWindow(const ProgressWindow&) = delete;
    ProgressWindow& operator=(const ProgressWindow&) = delete;

    bool create();
    HWND hwnd() const { return hwnd_; }
    std::atomic<bool>& cancelFlag() { return cancel_; }

    // Worker-thread safe: post-message wrappers.
    void postProgress (int overallPercent, const std::wstring& label);
    void postLogLine  (const std::wstring& line);
    void postCompleted(bool success, const std::wstring& summary);

private:
    static LRESULT CALLBACK wndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND, UINT, WPARAM, LPARAM);
    void onCreate();
    void onResize(int w, int h);
    void appendLog(const std::wstring& s);
    void finalize(bool success, const std::wstring& summary);

    HWND owner_       = nullptr;
    HWND hwnd_        = nullptr;
    HWND hwndLabel_   = nullptr;
    HWND hwndBar_     = nullptr;
    HWND hwndLog_     = nullptr;
    HWND hwndCancel_  = nullptr;
    HFONT font_       = nullptr;
    HFONT fontBold_   = nullptr;

    std::atomic<bool> cancel_{ false };
    bool done_ = false;
};

} // namespace wid::gui
