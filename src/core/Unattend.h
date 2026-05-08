#pragma once

#include "core/Commands.h"

#include <filesystem>
#include <string>
#include <vector>

namespace wid::core {

namespace fs = std::filesystem;

struct UnattendOptions {
    std::wstring locale          = L"en-US";
    std::wstring timezone        = L"UTC";
    std::wstring computerName;       // empty = let setup pick
    std::wstring adminPassword;      // optional
    bool         skipMicrosoftAccount = true;
    bool         acceptEula           = true;
    bool         autoLogon            = false;
    std::wstring autoLogonUser;
    std::wstring autoLogonPassword;
    std::vector<ScriptedCommand> firstLogonCommands;  // post-logon, written into unattend
};

// isoSourcesDir is the <iso>\sources directory; autounattend.xml is written
// to its parent (the ISO root).
bool writeUnattendXml(const fs::path& isoSourcesDir,
                      const UnattendOptions& opts);

} // namespace wid::core
