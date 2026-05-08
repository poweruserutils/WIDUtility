#include "gui/ProgressWindow.h"

#include <commctrl.h>
#include <windowsx.h>
#include <memory>
#include <string>

namespace wid::gui {

namespace {

constexpr wchar_t kClass[]   = L"WIDUtility.Progress";
constexpr UINT    WM_PROG    = WM_APP + 1;   // wparam: percent (0..100), lparam: wstring*
constexpr UINT    WM_LOG     = WM_APP + 2;   // lparam: wstring*
constexpr UINT    WM_DONE    = WM_APP + 3;   // wparam: success bool, lparam: wstring*

constexpr WORD    ID_CANCEL  = 1;
constexpr int     kPad       = 14;

HFONT makeFont(int pt, bool bold = false) {
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    LOGFONTW lf{};
    lf.lfHeight  = -MulDiv(pt, dpi, 72);
    lf.lfWeight  = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

} // namespace

ProgressWindow::ProgressWindow(HWND owner) : owner_(owner) {}

ProgressWindow::~ProgressWindow() {
    if (font_)     DeleteObject(font_);
    if (fontBold_) DeleteObject(fontBold_);
    if (hwnd_)     DestroyWindow(hwnd_);
    if (owner_)    EnableWindow(owner_, TRUE);
}

LRESULT CALLBACK ProgressWindow::wndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
    ProgressWindow* self = nullptr;
    if (m == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        self = static_cast<ProgressWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->hwnd_ = h;
    } else {
        self = reinterpret_cast<ProgressWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (self) return self->wndProc(h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

bool ProgressWindow::create() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = wndProcThunk;
        wc.hInstance     = (HINSTANCE)GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    font_     = makeFont(9);
    fontBold_ = makeFont(10, true);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT or_; GetWindowRect(owner_, &or_);
    int W = 720, H = 460;
    int x = or_.left + ((or_.right - or_.left) - W) / 2;
    int y = or_.top  + ((or_.bottom - or_.top) - H) / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClass, L"Building ISO",
        style, x, y, W, H, owner_, nullptr,
        (HINSTANCE)GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;

    EnableWindow(owner_, FALSE);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void ProgressWindow::onCreate() {
    HINSTANCE hi = (HINSTANCE)GetModuleHandleW(nullptr);

    hwndLabel_ = CreateWindowExW(0, L"STATIC",
        L"Starting...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        kPad, kPad, 600, 24, hwnd_, nullptr, hi, nullptr);
    SendMessageW(hwndLabel_, WM_SETFONT, (WPARAM)fontBold_, TRUE);

    hwndBar_ = CreateWindowExW(0, PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE,
        kPad, kPad + 30, 680, 18, hwnd_, nullptr, hi, nullptr);
    SendMessageW(hwndBar_, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    hwndLog_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        kPad, kPad + 60, 680, 280, hwnd_, nullptr, hi, nullptr);
    SendMessageW(hwndLog_, WM_SETFONT, (WPARAM)font_, TRUE);

    hwndCancel_ = CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        720 - kPad - 100, kPad + 350, 100, 28,
        hwnd_, (HMENU)(INT_PTR)ID_CANCEL, hi, nullptr);
    SendMessageW(hwndCancel_, WM_SETFONT, (WPARAM)font_, TRUE);
}

void ProgressWindow::onResize(int w, int h) {
    if (hwndLog_)
        MoveWindow(hwndLog_, kPad, kPad + 60, w - 2*kPad, h - 60 - kPad - 50, TRUE);
    if (hwndBar_)
        MoveWindow(hwndBar_, kPad, kPad + 30, w - 2*kPad, 18, TRUE);
    if (hwndCancel_)
        MoveWindow(hwndCancel_, w - kPad - 100, h - kPad - 32, 100, 28, TRUE);
}

LRESULT ProgressWindow::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: onCreate(); return 0;

    case WM_SIZE: onResize(LOWORD(l), HIWORD(l)); return 0;

    case WM_COMMAND:
        if (LOWORD(w) == ID_CANCEL) {
            if (done_) {
                DestroyWindow(h);
            } else {
                cancel_.store(true);
                SetWindowTextW(hwndCancel_, L"Cancelling...");
                EnableWindow(hwndCancel_, FALSE);
                SetWindowTextW(hwndLabel_, L"Cancelling — finishing current step...");
            }
        }
        return 0;

    case WM_PROG: {
        std::unique_ptr<std::wstring> lbl((std::wstring*)l);
        SendMessageW(hwndBar_, PBM_SETPOS, (WPARAM)w, 0);
        if (lbl) SetWindowTextW(hwndLabel_, lbl->c_str());
        return 0;
    }
    case WM_LOG: {
        std::unique_ptr<std::wstring> ln((std::wstring*)l);
        if (ln) appendLog(*ln);
        return 0;
    }
    case WM_DONE: {
        std::unique_ptr<std::wstring> sum((std::wstring*)l);
        finalize(w != 0, sum ? *sum : std::wstring());
        return 0;
    }

    case WM_CLOSE:
        if (done_) DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        if (owner_) {
            EnableWindow(owner_, TRUE);
            SetForegroundWindow(owner_);
        }
        hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void ProgressWindow::appendLog(const std::wstring& s) {
    int len = GetWindowTextLengthW(hwndLog_);
    SendMessageW(hwndLog_, EM_SETSEL, len, len);
    std::wstring line = s + L"\r\n";
    SendMessageW(hwndLog_, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
}

void ProgressWindow::finalize(bool success, const std::wstring& summary) {
    done_ = true;
    SendMessageW(hwndBar_, PBM_SETPOS, success ? 100 : 0, 0);
    SetWindowTextW(hwndLabel_, success ? L"Build complete." : L"Build failed.");
    SetWindowTextW(hwndCancel_, L"Close");
    EnableWindow(hwndCancel_, TRUE);
    if (!summary.empty()) appendLog(L"--- " + summary + L" ---");
}

void ProgressWindow::postProgress(int percent, const std::wstring& label) {
    if (!hwnd_) return;
    auto* p = new std::wstring(label);
    if (!PostMessageW(hwnd_, WM_PROG, (WPARAM)percent, (LPARAM)p)) delete p;
}

void ProgressWindow::postLogLine(const std::wstring& line) {
    if (!hwnd_) return;
    auto* p = new std::wstring(line);
    if (!PostMessageW(hwnd_, WM_LOG, 0, (LPARAM)p)) delete p;
}

void ProgressWindow::postCompleted(bool success, const std::wstring& summary) {
    if (!hwnd_) return;
    auto* p = new std::wstring(summary);
    if (!PostMessageW(hwnd_, WM_DONE, success ? 1 : 0, (LPARAM)p)) delete p;
}

} // namespace wid::gui
