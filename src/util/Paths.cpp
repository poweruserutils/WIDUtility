#include "util/Paths.h"

#include <windows.h>
#include <shlobj.h>
#include <chrono>
#include <sstream>

namespace wid::util {

namespace {

std::optional<fs::path> envPath(const wchar_t* var) {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetEnvironmentVariableW(var, buf, (DWORD)std::size(buf));
    if (n == 0 || n >= std::size(buf)) return std::nullopt;
    return fs::path(buf);
}

std::optional<fs::path> existing(const fs::path& p) {
    std::error_code ec;
    if (fs::exists(p, ec)) return p;
    return std::nullopt;
}

} // namespace

std::optional<fs::path> findDism() {
    if (auto sys = envPath(L"SystemRoot"))
        if (auto p = existing(*sys / L"System32" / L"Dism.exe")) return p;
    return std::nullopt;
}

std::optional<fs::path> findReg() {
    if (auto sys = envPath(L"SystemRoot"))
        if (auto p = existing(*sys / L"System32" / L"reg.exe")) return p;
    return std::nullopt;
}

std::optional<fs::path> findOscdimg() {
    static const wchar_t* kEnvVars[] = {
        L"ProgramFiles(x86)", L"ProgramFiles", L"ProgramW6432"
    };
    static const wchar_t* kArchSubdirs[] = { L"amd64", L"x86", L"arm64" };

    for (auto* var : kEnvVars) {
        auto root = envPath(var);
        if (!root) continue;
        fs::path adk = *root / L"Windows Kits" / L"10" /
                       L"Assessment and Deployment Kit" /
                       L"Deployment Tools";
        std::error_code ec;
        if (!fs::exists(adk, ec)) continue;
        for (auto* arch : kArchSubdirs) {
            fs::path candidate = adk / arch / L"Oscdimg" / L"oscdimg.exe";
            if (auto p = existing(candidate)) return p;
        }
    }
    return std::nullopt;
}

fs::path createScratchRoot() {
    auto tmp = envPath(L"TEMP").value_or(fs::temp_directory_path());
    using namespace std::chrono;
    auto now = system_clock::now().time_since_epoch();
    auto ms  = duration_cast<milliseconds>(now).count();

    std::wstringstream ss;
    ss << L"WIDUtility_" << ms;
    fs::path root = tmp / L"WIDUtility" / ss.str();
    std::error_code ec;
    fs::create_directories(root, ec);
    return root;
}

fs::path isoExtractDir(const fs::path& r) { return r / L"iso";   }
fs::path wimMountDir  (const fs::path& r) { return r / L"mount"; }
fs::path stageDir     (const fs::path& r) { return r / L"stage"; }

fs::path desktopLogsDir() {
    fs::path desk;
    wchar_t buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY,
                                   nullptr, 0, buf))) {
        desk = buf;
    } else if (auto u = envPath(L"USERPROFILE")) {
        desk = *u / L"Desktop";
    }
    fs::path d = desk / L"WID Utility Logs";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

fs::path currentLogFile() {
    static fs::path cached;
    if (!cached.empty()) return cached;

    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t name[64];
    swprintf_s(name, L"WID-%04d%02d%02d-%02d%02d%02d.log",
               t.wYear, t.wMonth, t.wDay,
               t.wHour, t.wMinute, t.wSecond);
    cached = desktopLogsDir() / name;
    return cached;
}

// Try to release any DISM-mounted WIM under <scratch>/mount. A previous
// crashed run may leave one mounted, which then blocks remove_all on
// the whole scratch dir. Best-effort: any error is ignored — if there's
// nothing mounted, dism just exits non-zero and we move on.
static void tryReleaseStaleMount(const fs::path& scratch) {
    fs::path mountDir = scratch / L"mount";
    std::error_code ec;
    if (!fs::exists(mountDir, ec)) return;

    auto dism = findDism();
    if (!dism) return;

    std::wstring cmd = L"\"" + dism->wstring() + L"\" /English /Quiet "
                       L"/Unmount-Image /MountDir:\"" +
                       mountDir.wstring() + L"\" /Discard";

    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring mut = cmd;
    if (CreateProcessW(nullptr, mut.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        // Cap at 5 minutes; a /Discard unmount is normally fast but can
        // be slow if the WIM is large or the system is loaded.
        WaitForSingleObject(pi.hProcess, 300000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void cleanScratchRoot(const fs::path& root) {
    std::error_code ec;
    if (!fs::exists(root, ec)) return;
    tryReleaseStaleMount(root);
    fs::remove_all(root, ec);
}

void cleanOldScratchRoots() {
    auto tmp = envPath(L"TEMP").value_or(fs::temp_directory_path());
    fs::path root = tmp / L"WIDUtility";
    std::error_code ec;
    if (!fs::exists(root, ec)) return;
    for (auto& entry : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (entry.is_directory(ec)) cleanScratchRoot(entry.path());
    }
}

} // namespace wid::util
