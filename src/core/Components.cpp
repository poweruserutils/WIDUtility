#include "core/Components.h"

#include "util/Log.h"
#include "util/Paths.h"
#include "util/Process.h"

#include <sstream>

namespace wid::core {

namespace {

std::wstring trim(std::wstring s) {
    while (!s.empty() && (s.back() == L'\r' || s.back() == L' ' || s.back() == L'\t'))
        s.pop_back();
    size_t start = 0;
    while (start < s.size() && (s[start] == L' ' || s[start] == L'\t')) ++start;
    return s.substr(start);
}

util::ProcessResult runDismCapture(std::vector<std::wstring> args) {
    util::ProcessResult empty;
    auto dism = util::findDism();
    if (!dism) return empty;
    util::ProcessOptions po;
    po.executable = dism->wstring();
    po.args = std::move(args);
    return util::run(po);
}

} // namespace

std::vector<ProvisionedAppx> listProvisionedAppx(const fs::path& mountDir,
                                                 const ProgressFn& progress) {
    std::vector<ProvisionedAppx> out;
    auto res = runDismCapture({
        L"/English",
        L"/Image:" + mountDir.wstring(),
        L"/Get-ProvisionedAppxPackages"
    });
    if (!res.finished || res.exitCode != 0) return out;

    std::wistringstream ss(res.stdoutText);
    std::wstring line;
    ProvisionedAppx cur;
    auto flush = [&]() {
        if (!cur.packageName.empty()) out.push_back(cur);
        cur = {};
    };
    while (std::getline(ss, line)) {
        line = trim(std::move(line));
        if (line.empty()) { flush(); continue; }
        auto colon = line.find(L':');
        if (colon == std::wstring::npos) continue;
        std::wstring k = trim(line.substr(0, colon));
        std::wstring v = trim(line.substr(colon + 1));
        if      (k == L"DisplayName") cur.displayName = v;
        else if (k == L"PackageName") cur.packageName = v;
    }
    flush();
    if (progress) progress(L"Listing provisioned packages", 100);
    return out;
}

std::vector<OptionalFeature> listOptionalFeatures(const fs::path& mountDir,
                                                  const ProgressFn& progress) {
    std::vector<OptionalFeature> out;
    auto res = runDismCapture({
        L"/English",
        L"/Image:" + mountDir.wstring(),
        L"/Get-Features",
        L"/Format:Table"
    });
    if (!res.finished || res.exitCode != 0) return out;

    std::wistringstream ss(res.stdoutText);
    std::wstring line;
    bool inTable = false;
    while (std::getline(ss, line)) {
        line = trim(std::move(line));
        if (line.empty()) continue;
        if (line.find(L"---") == 0) { inTable = !inTable; continue; }
        if (!inTable) continue;
        // "FeatureName | State"
        auto bar = line.find(L'|');
        if (bar == std::wstring::npos) continue;
        OptionalFeature f;
        f.name  = trim(line.substr(0, bar));
        f.state = trim(line.substr(bar + 1));
        if (!f.name.empty()) out.push_back(f);
    }
    if (progress) progress(L"Listing optional features", 100);
    return out;
}

std::vector<Capability> listCapabilities(const fs::path& mountDir,
                                         const ProgressFn& progress) {
    std::vector<Capability> out;
    auto res = runDismCapture({
        L"/English",
        L"/Image:" + mountDir.wstring(),
        L"/Get-Capabilities",
        L"/Format:Table"
    });
    if (!res.finished || res.exitCode != 0) return out;

    std::wistringstream ss(res.stdoutText);
    std::wstring line;
    bool inTable = false;
    while (std::getline(ss, line)) {
        line = trim(std::move(line));
        if (line.empty()) continue;
        if (line.find(L"---") == 0) { inTable = !inTable; continue; }
        if (!inTable) continue;
        auto bar = line.find(L'|');
        if (bar == std::wstring::npos) continue;
        Capability c;
        c.name  = trim(line.substr(0, bar));
        c.state = trim(line.substr(bar + 1));
        if (!c.name.empty()) out.push_back(c);
    }
    if (progress) progress(L"Listing capabilities", 100);
    return out;
}

} // namespace wid::core
