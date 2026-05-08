#include "util/Process.h"

#include <windows.h>
#include <thread>

namespace wid::util {

namespace {

std::wstring buildCommandLine(const ProcessOptions& opts) {
    std::wstring cl = quoteArg(opts.executable);
    for (const auto& a : opts.args) {
        cl.push_back(L' ');
        cl.append(quoteArg(a));
    }
    return cl;
}

// Drain a pipe handle on a worker thread, append to `out`, and call `cb` per line.
void pumpPipe(HANDLE hRead, std::wstring& out, bool fromStderr, const LineCallback& cb) {
    std::string acc;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(hRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
        acc.append(buf, buf + n);
        size_t start = 0;
        for (size_t i = 0; i < acc.size(); ++i) {
            if (acc[i] == '\n') {
                std::string line = acc.substr(start, i - start);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                int wlen = MultiByteToWideChar(CP_UTF8, 0, line.data(),
                                               (int)line.size(), nullptr, 0);
                std::wstring wline(wlen, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, line.data(), (int)line.size(),
                                    wline.data(), wlen);
                out.append(wline).push_back(L'\n');
                if (cb) cb(wline, fromStderr);
                start = i + 1;
            }
        }
        acc.erase(0, start);
    }
    if (!acc.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, acc.data(),
                                       (int)acc.size(), nullptr, 0);
        std::wstring wline(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, acc.data(), (int)acc.size(),
                            wline.data(), wlen);
        out.append(wline);
        if (cb) cb(wline, fromStderr);
    }
}

} // namespace

std::wstring quoteArg(std::wstring_view arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"\n\v") == std::wstring_view::npos) {
        return std::wstring(arg);
    }
    std::wstring out;
    out.push_back(L'"');
    for (size_t i = 0; i < arg.size(); ++i) {
        size_t bs = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++bs; ++i; }
        if (i == arg.size()) {
            out.append(bs * 2, L'\\');
            break;
        } else if (arg[i] == L'"') {
            out.append(bs * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(bs, L'\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

ProcessResult run(const ProcessOptions& opts) {
    ProcessResult res;

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };

    HANDLE outR = nullptr, outW = nullptr;
    HANDLE errR = nullptr, errW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0)) return res;
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    if (!CreatePipe(&errR, &errW, &sa, 0)) {
        CloseHandle(outR); CloseHandle(outW);
        return res;
    }
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);

    std::wstring cl = buildCommandLine(opts);
    std::vector<wchar_t> mutCl(cl.begin(), cl.end());
    mutCl.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | (opts.hideWindow ? STARTF_USESHOWWINDOW : 0);
    si.wShowWindow = opts.hideWindow ? SW_HIDE : SW_SHOWDEFAULT;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = outW;
    si.hStdError  = errW;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(
        nullptr, mutCl.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW,
        nullptr,
        opts.workingDir.empty() ? nullptr : opts.workingDir.c_str(),
        &si, &pi);

    CloseHandle(outW);
    CloseHandle(errW);

    if (!ok) {
        CloseHandle(outR);
        CloseHandle(errR);
        return res;
    }
    res.launched = true;

    std::thread tOut(pumpPipe, outR, std::ref(res.stdoutText), false, opts.onLine);
    std::thread tErr(pumpPipe, errR, std::ref(res.stderrText), true,  opts.onLine);

    DWORD waitRes = WaitForSingleObject(pi.hProcess, opts.timeoutMs);
    if (waitRes == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 0xFFFFFFFF);
        WaitForSingleObject(pi.hProcess, INFINITE);
    }

    GetExitCodeProcess(pi.hProcess, &res.exitCode);
    res.finished = (waitRes == WAIT_OBJECT_0);

    tOut.join();
    tErr.join();

    CloseHandle(outR);
    CloseHandle(errR);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return res;
}

} // namespace wid::util
