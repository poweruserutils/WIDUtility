#pragma once

#include "core/Hive.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace wid::core {

namespace fs = std::filesystem;

// A tweak applies offline edits to the mounted image. Some tweaks edit
// hives, some replace files, some write Start-menu shortcuts.
struct TweakContext {
    fs::path mountDir;             // <mount> root
    fs::path softwareHive;         // <mount>\Windows\System32\config\SOFTWARE
    fs::path systemHive;           // <mount>\Windows\System32\config\SYSTEM
    fs::path defaultUserHive;      // <mount>\Users\Default\NTUSER.DAT
};

using TweakApplyFn = std::function<bool(const TweakContext&)>;

struct TweakEntry {
    std::wstring id;            // "tweak.uac.disable"
    std::wstring displayName;
    std::wstring description;
    std::wstring category;       // "Privacy", "Security", "UI", "Boot", ...
    bool         dangerous = false;  // shows a warning in GUI
    TweakApplyFn apply;
};

const std::vector<TweakEntry>& tweakCatalog();

// Helpers used by tweak implementations and exposed for unit tests.
bool applyUacDisable          (const TweakContext&);
bool applyVerboseStatus       (const TweakContext&);
bool applySethcSwap           (const TweakContext&);   // sethc.exe → cmd.exe
bool applyLabConfigBypasses   (const TweakContext&);   // TPM/SecureBoot/RAM/CPU
bool applyAllowUnsupportedTpm (const TweakContext&);
bool installRebootToUefiShortcut       (const TweakContext&);
bool installRebootToBootDeviceShortcut (const TweakContext&);

} // namespace wid::core
