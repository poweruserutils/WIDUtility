#pragma once

#include "core/Hive.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace wid::core {

namespace fs = std::filesystem;

// Accumulates lines for a Windows .reg file. Tweaks that need registry
// changes append to this instead of editing offline hives directly: the
// pipeline writes the script into the image and SetupComplete.cmd
// imports it on the target machine after OOBE, before first logon.
//
// We use this path because direct offline-hive editing via RegLoadKey
// is rejected by Win11's loader (see buildfailfixes.md).
class RegScript {
public:
    // root: "HKLM" or "HKCU" etc. path: subkey path under root.
    void setDword (const std::wstring& root, const std::wstring& path,
                   const std::wstring& name, unsigned int value);
    void setString(const std::wstring& root, const std::wstring& path,
                   const std::wstring& name, const std::wstring& value);
    void deleteValue(const std::wstring& root, const std::wstring& path,
                     const std::wstring& name);
    void deleteKey  (const std::wstring& root, const std::wstring& path);

    bool empty() const { return body_.empty(); }

    // Full .reg file text including the v5 header.
    std::wstring toRegFile() const;

private:
    std::wstring body_;
};

// A tweak applies offline edits to the mounted image. Some tweaks edit
// hives, some replace files, some write Start-menu shortcuts.
struct TweakContext {
    fs::path    mountDir;          // <mount> root
    fs::path    softwareHive;      // <mount>\Windows\System32\config\SOFTWARE
    fs::path    systemHive;        // <mount>\Windows\System32\config\SYSTEM
    fs::path    defaultUserHive;   // <mount>\Users\Default\NTUSER.DAT
    RegScript*  regScript = nullptr;  // shared accumulator (non-owning)
    std::vector<std::wstring>* setupCmdLines = nullptr;  // SetupComplete.cmd extras
};

// Write the accumulated RegScript into <mount>\Windows\Setup\Scripts\
// WID-tweaks.reg. Idempotent; safe to call with an empty script
// (returns true, writes nothing). Caller is responsible for asking
// SetupComplete.cmd to import it (see regImportSetupCompleteLine()).
bool writeRegScriptFile(const TweakContext& ctx);

// The exact .cmd line that imports WID-tweaks.reg. Caller appends this
// to the SetupComplete.cmd writer's extras list so a single writer
// owns the cmd file (avoids collisions with user pre-logon commands).
std::wstring regImportSetupCompleteLine();

// Mount <iso>/sources/boot.wim index 2 (the Setup environment), inject
// LabConfig hardware-bypass DWORDs and MoSetup\AllowUpgradesWithUnsupportedTPMOrCPU
// into its SYSTEM hive, and commit. This is the only path that actually
// makes Setup skip the TPM / SecureBoot / RAM / CPU checks during install
// — the SetupComplete.cmd .reg import is too late (Setup has already
// finished by the time it runs). Returns true on success or false with
// the reason logged.
bool applyBootWimHardwareBypass(const fs::path& isoDir,
                                const fs::path& scratchRoot);

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
