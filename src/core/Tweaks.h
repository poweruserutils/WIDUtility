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
};

// Materialize the accumulated RegScript into <mount>\Windows\Setup\Scripts\
// as WID-tweaks.reg + SetupComplete.cmd. Idempotent; safe to call with
// an empty script (does nothing). Returns false on file I/O error.
bool writeSetupCompleteFromRegScript(const TweakContext& ctx);

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
