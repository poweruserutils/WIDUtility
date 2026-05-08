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

} // namespace wid::util
