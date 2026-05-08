#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace wid::core {

namespace fs = std::filesystem;

enum class CommandPhase {
    PreLogon,    // SetupComplete.cmd / synchronous unattend commands (SYSTEM context)
    PostLogon,   // FirstLogonCommands / RunOnce (user context, after first logon)
};

struct ScriptedCommand {
    std::wstring description;       // human-readable label
    std::wstring commandLine;       // raw command (cmd.exe-compatible)
    bool         continueOnError = false;
};

class CommandSet {
public:
    void add(CommandPhase phase, ScriptedCommand cmd);
    void clear(CommandPhase phase);
    void remove(CommandPhase phase, std::size_t index);
    bool moveUp(CommandPhase phase, std::size_t index);
    bool moveDown(CommandPhase phase, std::size_t index);

    const std::vector<ScriptedCommand>& list(CommandPhase phase) const;

    // Emit SetupComplete.cmd into <mount>\Windows\Setup\Scripts\.
    // Pre-logon commands plus any installer-staging entries are written here.
    bool writeSetupComplete(const fs::path& mountDir,
                            const std::vector<std::wstring>& extraPreLogonLines = {}) const;

private:
    std::vector<ScriptedCommand> pre_;
    std::vector<ScriptedCommand> post_;
};

} // namespace wid::core
