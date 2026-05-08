#pragma once

#include "core/Iso.h"

#include <filesystem>
#include <string>
#include <vector>

namespace wid::core {

namespace fs = std::filesystem;

struct WimEdition {
    int          index = 0;
    std::wstring name;          // "Windows 11 Pro"
    std::wstring description;
    std::wstring architecture;  // "x64", "ARM64"
    std::uint64_t sizeBytes = 0;
};

struct WimMount {
    fs::path wimFile;
    int      index = 0;
    fs::path mountDir;
    bool     readWrite = true;
};

std::vector<WimEdition> getWimInfo(const fs::path& wimFile, const ProgressFn& progress);

bool mountWim   (const WimMount& m, const ProgressFn& progress);
bool unmountWim (const WimMount& m, bool commit, const ProgressFn& progress);

bool exportImage(const fs::path& srcWim, int srcIndex,
                 const fs::path& dstWim, const std::wstring& compress,
                 const ProgressFn& progress);

// DISM /Image:<mount> wrappers.
bool removeProvisionedAppx(const fs::path& mountDir, const std::wstring& packageName,
                           const ProgressFn& progress);
bool disableFeature       (const fs::path& mountDir, const std::wstring& featureName,
                           const ProgressFn& progress);
bool enableFeature        (const fs::path& mountDir, const std::wstring& featureName,
                           const ProgressFn& progress);
bool removeCapability     (const fs::path& mountDir, const std::wstring& capabilityName,
                           const ProgressFn& progress);
bool addDriver            (const fs::path& mountDir, const fs::path& infFile,
                           const ProgressFn& progress);
bool addPackage           (const fs::path& mountDir, const fs::path& packageFile,
                           const ProgressFn& progress);

} // namespace wid::core
