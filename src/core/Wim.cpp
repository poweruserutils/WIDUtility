#include "core/Wim.h"

#include "util/Log.h"
#include "util/Paths.h"
#include "util/Process.h"

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
    return res.launched && res.finished && res.exitCode == 0;
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
    auto res = util::run(po);
    if (!res.launched || !res.finished || res.exitCode != 0) return editions;

    // Parse blocks separated by blank lines.
    std::wistringstream ss(res.stdoutText);
    std::wstring line;
    WimEdition cur;
    auto flush = [&]() {
        if (cur.index > 0) editions.push_back(cur);
        cur = WimEdition{};
    };
    while (std::getline(ss, line)) {
        auto trim = [](std::wstring& s){
            while (!s.empty() && (s.back() == L'\r' || s.back() == L' ')) s.pop_back();
        };
        trim(line);
        if (line.empty()) { flush(); continue; }
        auto colon = line.find(L':');
        if (colon == std::wstring::npos) continue;
        std::wstring key = line.substr(0, colon);
        std::wstring val = line.substr(colon + 1);
        while (!val.empty() && val.front() == L' ') val.erase(val.begin());

        if      (key == L"Index")        cur.index = std::stoi(val);
        else if (key == L"Name")         cur.name = val;
        else if (key == L"Description")  cur.description = val;
        else if (key == L"Architecture") cur.architecture = val;
        else if (key == L"Size")         {
            std::wstring digits;
            for (wchar_t c : val) if (iswdigit(c)) digits.push_back(c);
            if (!digits.empty()) cur.sizeBytes = std::stoull(digits);
        }
    }
    flush();

    if (progress) progress(L"Inspecting WIM", 100);
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
