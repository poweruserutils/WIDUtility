#include "core/Wim.h"

#include "util/Log.h"
#include "util/Paths.h"
#include "util/Process.h"

#include <windows.h>
#include <initguid.h>
#include <virtdisk.h>
#include <regex>
#include <sstream>

namespace wid::core {

namespace {

bool runDism(std::vector<std::wstring> args, const ProgressFn& progress,
             const std::wstring& stage) {
    auto dism = util::findDism();
    if (!dism) {
        util::Log::instance().error(L"DISM not found.", L"Wim");
        return false;
    }
    util::ProcessOptions po;
    po.executable = dism->wstring();
    po.args = std::move(args);
    po.onLine = [&](std::wstring_view line, bool /*err*/) {
        util::Log::instance().debug(std::wstring(line), L"dism");
        // DISM emits lines like "[==========                 ] 25.0%"
        std::wstring s(line);
        auto pct = s.find(L"%");
        if (pct != std::wstring::npos && pct >= 4) {
            try {
                size_t start = pct;
                while (start > 0 && (iswdigit(s[start - 1]) || s[start - 1] == L'.'))
                    --start;
                int p = (int)std::stod(s.substr(start, pct - start));
                if (progress) progress(stage, p);
            } catch (...) {}
        }
    };
    auto res = util::run(po);
    bool ok = res.launched && res.finished && res.exitCode == 0;
    if (!ok) {
        util::Log::instance().error(
            stage + L": dism failed (launched=" +
            (res.launched ? L"1" : L"0") + L", finished=" +
            (res.finished ? L"1" : L"0") + L", exit=" +
            std::to_wstring(res.exitCode) + L")",
            L"Wim");
    }
    return ok;
}

} // namespace

std::vector<WimEdition> getWimInfo(const fs::path& wimFile, const ProgressFn& progress) {
    std::vector<WimEdition> editions;
    auto dism = util::findDism();
    if (!dism) return editions;

    util::ProcessOptions po;
    po.executable = dism->wstring();
    po.args = { L"/English", L"/Get-WimInfo",
                L"/WimFile:" + wimFile.wstring() };
    po.timeoutMs = 60000;
    auto res = util::run(po);
    util::Log::instance().debug(
        L"getWimInfo exit=" + std::to_wstring(res.exitCode) +
        L" stdout bytes=" + std::to_wstring(res.stdoutText.size()),
        L"Wim");
    if (!res.launched || !res.finished || res.exitCode != 0) return editions;

    auto trimBoth = [](std::wstring& s){
        while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' ||
                              s.back() == L' '  || s.back() == L'\t'))
            s.pop_back();
        size_t i = 0;
        while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) ++i;
        s.erase(0, i);
    };

    // DISM emits "Key : Value" with spaces around the colon. Trim both
    // sides so "Index " matches "Index", etc.
    std::wistringstream ss(res.stdoutText);
    std::wstring line;
    WimEdition cur;
    auto flush = [&]() {
        if (cur.index > 0) editions.push_back(cur);
        cur = WimEdition{};
    };
    while (std::getline(ss, line)) {
        trimBoth(line);
        if (line.empty()) { flush(); continue; }
        auto colon = line.find(L':');
        if (colon == std::wstring::npos) continue;
        std::wstring key = line.substr(0, colon);
        std::wstring val = line.substr(colon + 1);
        trimBoth(key);
        trimBoth(val);

        try {
            if      (key == L"Index")        cur.index = std::stoi(val);
            else if (key == L"Name")         cur.name = val;
            else if (key == L"Description")  cur.description = val;
            else if (key == L"Architecture") cur.architecture = val;
            else if (key == L"Size") {
                std::wstring digits;
                for (wchar_t c : val) if (iswdigit(c)) digits.push_back(c);
                if (!digits.empty()) cur.sizeBytes = std::stoull(digits);
            }
        } catch (...) { /* skip malformed line */ }
    }
    flush();

    if (progress) progress(L"Inspecting WIM", 100);
    return editions;
}

namespace {

wchar_t findIsoDriveByContent() {
    // Scan all logical drives for one whose root looks like a Windows
    // installation media tree. Used as a fallback when AttachVirtualDisk
    // didn't give us a drive letter (e.g. ISO was already attached).
    DWORD mask = GetLogicalDrives();
    for (int b = 0; b < 26; ++b) {
        if (!(mask & (1u << b))) continue;
        wchar_t d = (wchar_t)(L'A' + b);
        wchar_t root[8] = { d, L':', L'\\', 0 };
        UINT t = GetDriveTypeW(root);
        if (t != DRIVE_CDROM && t != DRIVE_REMOVABLE && t != DRIVE_FIXED)
            continue;
        std::error_code ec;
        std::wstring base = std::wstring(1, d) + L":";
        if (fs::exists(fs::path(base + L"\\sources\\install.wim"), ec) ||
            fs::exists(fs::path(base + L"\\sources\\install.esd"), ec)) {
            return d;
        }
    }
    return 0;
}

wchar_t newlyAppearedDrive(DWORD before) {
    DWORD now = GetLogicalDrives();
    DWORD diff = now & ~before;
    if (!diff) return 0;
    for (int b = 0; b < 26; ++b)
        if (diff & (1u << b)) return (wchar_t)(L'A' + b);
    return 0;
}

} // namespace

std::vector<WimEdition> inspectIso(const fs::path& iso, const ProgressFn& progress) {
    auto& log = util::Log::instance();
    log.info(L"inspectIso start: " + iso.wstring(), L"inspectIso");

    DWORD before = GetLogicalDrives();

    VIRTUAL_STORAGE_TYPE vst{};
    vst.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_ISO;
    vst.VendorId = VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT;

    HANDLE handle  = INVALID_HANDLE_VALUE;
    bool   weOwnAttach = false;

    DWORD r = OpenVirtualDisk(&vst, iso.c_str(),
        (VIRTUAL_DISK_ACCESS_MASK)(VIRTUAL_DISK_ACCESS_READ |
                                   VIRTUAL_DISK_ACCESS_GET_INFO),
        OPEN_VIRTUAL_DISK_FLAG_NONE, nullptr, &handle);
    if (r != ERROR_SUCCESS) {
        log.warn(L"OpenVirtualDisk failed (" + std::to_wstring(r) + L")",
                 L"inspectIso");
    } else {
        ATTACH_VIRTUAL_DISK_PARAMETERS avdp{};
        avdp.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
        DWORD ar = AttachVirtualDisk(handle, nullptr,
            (ATTACH_VIRTUAL_DISK_FLAG)(ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY),
            0, &avdp, nullptr);
        if (ar == ERROR_SUCCESS) {
            weOwnAttach = true;
            log.info(L"AttachVirtualDisk succeeded", L"inspectIso");
        } else {
            log.warn(L"AttachVirtualDisk returned " + std::to_wstring(ar) +
                     L" (likely already attached)", L"inspectIso");
        }
    }

    wchar_t driveLetter = 0;
    for (int i = 0; i < 60 && !driveLetter; ++i) {
        Sleep(100);
        driveLetter = newlyAppearedDrive(before);
    }

    if (!driveLetter) {
        driveLetter = findIsoDriveByContent();
        if (driveLetter)
            log.info(std::wstring(L"Located ISO content on drive ") +
                     driveLetter + L": (fallback scan)", L"inspectIso");
    } else {
        log.info(std::wstring(L"New drive letter from attach: ") +
                 driveLetter + L":", L"inspectIso");
    }

    std::vector<WimEdition> editions;
    if (driveLetter) {
        std::wstring d(1, driveLetter);
        const wchar_t* candidates[] = {
            L":\\sources\\install.wim",
            L":\\sources\\install.esd",
            L":\\x64\\sources\\install.wim",
            L":\\x64\\sources\\install.esd",
            L":\\x86\\sources\\install.wim",
            L":\\x86\\sources\\install.esd",
        };
        fs::path wim;
        std::error_code ec;
        for (auto* sub : candidates) {
            fs::path p = d + sub;
            if (fs::exists(p, ec)) { wim = p; break; }
        }
        if (!wim.empty()) {
            log.info(L"Reading WIM: " + wim.wstring(), L"inspectIso");
            editions = getWimInfo(wim, progress);
        } else {
            log.warn(L"No install.wim/.esd found on drive " + d, L"inspectIso");
        }
    } else {
        log.error(L"Could not locate the ISO drive letter (attach failed and "
                  L"no matching drive present).", L"inspectIso");
    }

    if (handle != INVALID_HANDLE_VALUE) {
        if (weOwnAttach)
            DetachVirtualDisk(handle, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
        CloseHandle(handle);
    }

    log.info(L"inspectIso done: " + std::to_wstring(editions.size()) +
             L" edition(s)", L"inspectIso");
    return editions;
}

bool mountWim(const WimMount& m, const ProgressFn& progress) {
    std::error_code ec;
    fs::create_directories(m.mountDir, ec);
    std::vector<std::wstring> args = {
        L"/English",
        L"/Mount-Image",
        L"/ImageFile:" + m.wimFile.wstring(),
        L"/Index:" + std::to_wstring(m.index),
        L"/MountDir:" + m.mountDir.wstring(),
    };
    if (!m.readWrite) args.push_back(L"/ReadOnly");
    return runDism(std::move(args), progress, L"Mounting WIM");
}

bool unmountWim(const WimMount& m, bool commit, const ProgressFn& progress) {
    std::vector<std::wstring> args = {
        L"/English",
        L"/Unmount-Image",
        L"/MountDir:" + m.mountDir.wstring(),
        commit ? L"/Commit" : L"/Discard",
    };
    return runDism(std::move(args), progress,
                   commit ? L"Committing WIM" : L"Discarding WIM");
}

bool exportImage(const fs::path& srcWim, int srcIndex,
                 const fs::path& dstWim, const std::wstring& compress,
                 const ProgressFn& progress) {
    std::vector<std::wstring> args = {
        L"/English",
        L"/Export-Image",
        L"/SourceImageFile:" + srcWim.wstring(),
        L"/SourceIndex:" + std::to_wstring(srcIndex),
        L"/DestinationImageFile:" + dstWim.wstring(),
        L"/Compress:" + (compress.empty() ? L"max" : compress),
    };
    return runDism(std::move(args), progress, L"Exporting edition");
}

bool removeProvisionedAppx(const fs::path& mountDir, const std::wstring& pkg,
                           const ProgressFn& progress) {
    return runDism({
        L"/English",
        L"/Image:" + mountDir.wstring(),
        L"/Remove-ProvisionedAppxPackage",
        L"/PackageName:" + pkg
    }, progress, L"Removing provisioned package");
}

bool disableFeature(const fs::path& mountDir, const std::wstring& feature,
                    const ProgressFn& progress) {
    return runDism({
        L"/English",
        L"/Image:" + mountDir.wstring(),
        L"/Disable-Feature",
        L"/FeatureName:" + feature,
        L"/Remove"
    }, progress, L"Disabling feature");
}

bool enableFeature(const fs::path& mountDir, const std::wstring& feature,
                   const ProgressFn& progress) {
    return runDism({
        L"/English",
        L"/Image:" + mountDir.wstring(),
        L"/Enable-Feature",
        L"/FeatureName:" + feature,
        L"/All"
    }, progress, L"Enabling feature");
}

bool removeCapability(const fs::path& mountDir, const std::wstring& cap,
                      const ProgressFn& progress) {
    return runDism({
        L"/English",
        L"/Image:" + mountDir.wstring(),
        L"/Remove-Capability",
        L"/CapabilityName:" + cap
    }, progress, L"Removing capability");
}

bool addDriver(const fs::path& mountDir, const fs::path& inf,
               const ProgressFn& progress) {
    return runDism({
        L"/English",
        L"/Image:" + mountDir.wstring(),
        L"/Add-Driver",
        L"/Driver:" + inf.wstring(),
        L"/Recurse"
    }, progress, L"Adding driver");
}

bool addPackage(const fs::path& mountDir, const fs::path& pkg,
                const ProgressFn& progress) {
    return runDism({
        L"/English",
        L"/Image:" + mountDir.wstring(),
        L"/Add-Package",
        L"/PackagePath:" + pkg.wstring()
    }, progress, L"Adding package");
}

} // namespace wid::core
