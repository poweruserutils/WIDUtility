#pragma once

#include <string>
#include <vector>

namespace wid::core {

// A third-party application available in the catalog. Installer can be
// retrieved from `downloadUrl` (TBD: download manager) or supplied locally
// by the user via `localPath`.
struct AppEntry {
    std::wstring id;             // stable id, e.g. "app.brave"
    std::wstring displayName;    // shown in GUI
    std::wstring vendor;
    std::wstring downloadUrl;    // canonical installer URL
    std::wstring silentArgs;     // silent-install switches
    std::wstring localPath;      // user-provided installer (overrides URL if set)
    bool         builtin = true; // true for catalog entries; false = user-added
};

const std::vector<AppEntry>& builtinAppCatalog();

// Lookup by id. Returns nullptr if not present.
const AppEntry* findApp(const std::vector<AppEntry>& catalog, const std::wstring& id);

} // namespace wid::core
