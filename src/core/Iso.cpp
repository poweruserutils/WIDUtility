#include "core/Iso.h"

#include "util/Log.h"
#include "util/Paths.h"
#include "util/Process.h"

#include <windows.h>

namespace wid::core {

namespace {

// Try to mount the ISO as a virtual disk via PowerShell's Mount-DiskImage,
// xcopy contents to destDir, then dismount. PowerShell is universally
// available on supported Windows versions. /A-:R asks robocopy to strip
// the read-only attribute that CDFS sets on every file, since DISM
// refuses to mount install.wim R/W when the file is read-only.
bool extractViaMount(const fs::path& iso, const fs::path& dest) {
    util::ProcessOptions ps;
    ps.executable = L"powershell.exe";
    ps.args = {
        L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass",
        L"-Command",
        L"$ErrorActionPreference='Stop';"
        L"$img = Mount-DiskImage -ImagePath $env:WID_ISO -PassThru;"
        L"$drive = ($img | Get-Volume).DriveLetter + ':\\';"
        L"robocopy $drive $env:WID_DEST /MIR /A-:RSH "
            L"/NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null;"
        L"Dismount-DiskImage -ImagePath $env:WID_ISO | Out-Null;"
    };

    SetEnvironmentVariableW(L"WID_ISO",  iso.c_str());
    SetEnvironmentVariableW(L"WID_DEST", dest.c_str());

    auto res = util::run(ps);
    SetEnvironmentVariableW(L"WID_ISO",  nullptr);
    SetEnvironmentVariableW(L"WID_DEST", nullptr);

    // robocopy returns 0-7 for success.
    return res.launched && res.finished && res.exitCode <= 7;
}

// Belt-and-braces: walk the extracted tree and clear the read-only +
// hidden + system attributes from every file. Needed even with /A-:RSH
// because some Windows builds of robocopy ignore -:H on certain files.
void clearReadOnlyTree(const fs::path& root) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        DWORD a = GetFileAttributesW(it->path().c_str());
        if (a == INVALID_FILE_ATTRIBUTES) continue;
        DWORD cleaned = a & ~(FILE_ATTRIBUTE_READONLY |
                              FILE_ATTRIBUTE_HIDDEN   |
                              FILE_ATTRIBUTE_SYSTEM);
        if (cleaned != a)
            SetFileAttributesW(it->path().c_str(), cleaned);
    }
}

} // namespace

bool extractIso(const IsoExtractOptions& opts, const ProgressFn& progress) {
    if (progress) progress(L"Extracting ISO", 0);

    std::error_code ec;
    fs::create_directories(opts.destDir, ec);

    util::Log::instance().info(L"Extracting ISO: " + opts.sourceIso.wstring(),
                               L"Iso");
    bool ok = extractViaMount(opts.sourceIso, opts.destDir);
    if (ok) clearReadOnlyTree(opts.destDir);
    if (progress) progress(L"Extracting ISO", 100);
    return ok;
}

bool buildIso(const IsoBuildOptions& opts, const ProgressFn& progress) {
    if (progress) progress(L"Building ISO with oscdimg", 0);

    auto oscdimg = util::findOscdimg();
    if (!oscdimg) {
        util::Log::instance().error(
            L"oscdimg.exe not found. Install the Windows ADK Deployment Tools.",
            L"Iso");
        return false;
    }

    fs::path etfsboot = opts.sourceDir / L"boot"    / L"etfsboot.com";
    fs::path efisys   = opts.sourceDir / L"efi"     / L"microsoft" / L"boot" / L"efisys.bin";

    std::wstring bootData;
    if (opts.biosBootable && opts.uefiBootable) {
        bootData = L"2#p0,e,b" + etfsboot.wstring() +
                   L"#pEF,e,b" + efisys.wstring();
    } else if (opts.uefiBootable) {
        bootData = L"1#pEF,e,b" + efisys.wstring();
    } else if (opts.biosBootable) {
        bootData = L"1#p0,e,b" + etfsboot.wstring();
    }

    util::ProcessOptions po;
    po.executable = oscdimg->wstring();
    po.args = { L"-m", L"-o", L"-u2", L"-udfver102" };
    if (!bootData.empty()) po.args.push_back(L"-bootdata:" + bootData);
    if (!opts.volumeLabel.empty())
        po.args.push_back(L"-l" + opts.volumeLabel);
    po.args.push_back(opts.sourceDir.wstring());
    po.args.push_back(opts.destIso.wstring());

    po.onLine = [&](std::wstring_view line, bool /*err*/) {
        util::Log::instance().debug(std::wstring(line), L"oscdimg");
    };

    auto res = util::run(po);
    if (progress) progress(L"Building ISO with oscdimg", 100);
    return res.launched && res.finished && res.exitCode == 0;
}

} // namespace wid::core
