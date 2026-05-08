#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace wid::core {

enum class ChangeKind {
    Component,        // remove/keep an inbox component, capability, package
    Feature,          // enable/disable optional feature
    Application,      // bake third-party app installer
    Tweak,            // toggle from the tweaks catalog
    Command,          // pre/post-logon command
    Driver,           // inject driver
    Update,           // integrate .msu/.cab
    Edition,          // keep/drop a WIM edition (index)
    Unattended,       // OOBE answer
};

enum class ChangeAction { Add, Remove, Modify };

struct PendingChange {
    std::uint64_t id           = 0;        // monotonic
    ChangeKind    kind         = ChangeKind::Tweak;
    ChangeAction  action       = ChangeAction::Add;
    std::wstring  targetId;                // catalog id (e.g. "tweak.uac.disable", "app.brave")
    std::wstring  description;             // human-readable for the Pending pane
    std::wstring  payload;                  // extra data (path, args, value), kind-specific
    std::chrono::system_clock::time_point queuedAt = std::chrono::system_clock::now();
    bool          continueOnError = false;
};

} // namespace wid::core
