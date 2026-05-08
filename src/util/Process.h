#pragma once

#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace wid::util {

using LineCallback = std::function<void(std::wstring_view line, bool fromStderr)>;

struct ProcessResult {
    bool         launched   = false;  // CreateProcess succeeded
    bool         finished   = false;  // child exited (vs killed / hung)
    DWORD        exitCode   = 0;
    std::wstring stdoutText;
    std::wstring stderrText;
};

struct ProcessOptions {
    std::wstring executable;            // absolute path to .exe
    std::vector<std::wstring> args;     // each arg quoted automatically
    std::wstring workingDir;            // empty = inherit
    LineCallback onLine;                // optional, fires per line of stdout/stderr
    bool         hideWindow = true;
    DWORD        timeoutMs  = INFINITE;
};

ProcessResult run(const ProcessOptions& opts);

// Quote a single argv element per CommandLineToArgvW rules.
std::wstring quoteArg(std::wstring_view arg);

} // namespace wid::util
