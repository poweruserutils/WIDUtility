#pragma once

#include "core/Iso.h"

#include <filesystem>
#include <string>
#include <vector>

namespace wid::core {

namespace fs = std::filesystem;

struct ProvisionedAppx {
    std::wstring packageName;     // full PackageName (PublisherId_Version_…)
    std::wstring displayName;
};

struct OptionalFeature {
    std::wstring name;
    std::wstring state;           // Enabled / Disabled / DisabledWithPayloadRemoved
};

struct Capability {
    std::wstring name;
    std::wstring state;
};

std::vector<ProvisionedAppx> listProvisionedAppx(const fs::path& mountDir,
                                                 const ProgressFn& progress);
std::vector<OptionalFeature> listOptionalFeatures(const fs::path& mountDir,
                                                  const ProgressFn& progress);
std::vector<Capability>      listCapabilities    (const fs::path& mountDir,
                                                  const ProgressFn& progress);

} // namespace wid::core
