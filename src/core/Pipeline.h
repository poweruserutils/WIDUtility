#pragma once

#include "core/ChangeQueue.h"
#include "core/Commands.h"
#include "core/Iso.h"
#include "core/Unattend.h"
#include "core/Wim.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <vector>

namespace wid::core {

namespace fs = std::filesystem;

// Reports rough progress through the 11 named build stages from plan.md.
enum class Stage {
    ExtractIso,
    InspectWim,
    MountWim,
    ApplyRegistry,
    ApplyDism,
    StageInstallers,
    WriteScripts,
    UnmountWim,
    TrimEditions,
    BuildIso,
    Verify,
};

struct StageReport {
    Stage        stage;
    std::wstring label;
    int          percent;        // 0..100 within this stage
    int          overallPercent; // 0..100 overall
};

using PipelineProgress = std::function<void(const StageReport&)>;

struct PipelineInputs {
    fs::path                    sourceIso;
    fs::path                    outputIso;
    std::vector<int>            keepEditionIndices; // empty = keep all
    bool                        trimUnselected = true;
    CommandSet                  commands;
    UnattendOptions             unattend;
    std::vector<PendingChange>  changes;            // snapshot from ChangeQueue
};

class Pipeline {
public:
    explicit Pipeline(PipelineInputs inputs);

    bool run(const PipelineProgress& progress);
    void cancel() { cancel_.store(true); }

private:
    PipelineInputs    inputs_;
    fs::path          scratch_;
    std::atomic<bool> cancel_{ false };
};

} // namespace wid::core
