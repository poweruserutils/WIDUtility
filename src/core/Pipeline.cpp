#include "core/Pipeline.h"

#include "core/Apps.h"
#include "core/Hive.h"
#include "core/Tweaks.h"
#include "util/Log.h"
#include "util/Paths.h"

namespace wid::core {

namespace {

struct StageWeight { Stage stage; const wchar_t* label; int weight; };
const StageWeight kStages[] = {
    { Stage::ExtractIso,      L"Extracting ISO",                10 },
    { Stage::InspectWim,      L"Inspecting WIM",                 2 },
    { Stage::MountWim,        L"Mounting WIM",                  10 },
    { Stage::ApplyRegistry,   L"Applying registry edits",        5 },
    { Stage::ApplyDism,       L"Applying DISM operations",      25 },
    { Stage::StageInstallers, L"Staging third-party installers", 5 },
    { Stage::WriteScripts,    L"Writing setup scripts",          2 },
    { Stage::UnmountWim,      L"Committing and unmounting WIM", 15 },
    { Stage::TrimEditions,    L"Trimming editions",             10 },
    { Stage::BuildIso,        L"Building ISO with oscdimg",     14 },
    { Stage::Verify,          L"Verifying output",               2 },
};

int totalWeight() {
    int t = 0;
    for (auto& s : kStages) t += s.weight;
    return t;
}

} // namespace

Pipeline::Pipeline(PipelineInputs inputs) : inputs_(std::move(inputs)) {}

bool Pipeline::run(const PipelineProgress& progress) {
    scratch_ = util::createScratchRoot();
    auto& log = util::Log::instance();
    log.info(L"Pipeline scratch root: " + scratch_.wstring(), L"Pipeline");

    int doneWeight = 0;
    const int total = totalWeight();

    auto report = [&](Stage stage, const std::wstring& label, int pct) {
        if (!progress) return;
        StageReport r{};
        r.stage = stage;
        r.label = label;
        r.percent = pct;
        // overall = (sum of completed stage weights + current stage * pct) / total
        int cumulative = 0;
        for (auto& s : kStages) {
            if (s.stage == stage) {
                cumulative += s.weight * pct / 100;
                break;
            }
            cumulative += s.weight;
        }
        r.overallPercent = total > 0 ? (cumulative * 100 / total) : 0;
        progress(r);
    };

    auto stageProgress = [&](Stage stage, const std::wstring& label) {
        return [report, stage, label](std::wstring_view sub, int pct) {
            std::wstring combined = label;
            if (!sub.empty()) combined += L" — " + std::wstring(sub);
            report(stage, combined, pct);
        };
    };

    auto checkCancel = [&]() {
        if (cancel_.load()) {
            log.warn(L"Pipeline cancelled by user.", L"Pipeline");
            return true;
        }
        return false;
    };

    // Stage 1: extract ISO
    fs::path isoDir = util::isoExtractDir(scratch_);
    {
        IsoExtractOptions eo{ inputs_.sourceIso, isoDir };
        if (!extractIso(eo, stageProgress(Stage::ExtractIso, L"Extracting ISO"))) {
            log.error(L"ISO extraction failed.", L"Pipeline");
            return false;
        }
    }
    if (checkCancel()) return false;

    fs::path installWim = isoDir / L"sources" / L"install.wim";
    if (!fs::exists(installWim)) installWim = isoDir / L"sources" / L"install.esd";

    // Stage 2: inspect
    auto editions = getWimInfo(installWim, stageProgress(Stage::InspectWim, L"Inspecting WIM"));
    if (editions.empty()) {
        log.error(L"No editions found in install.wim.", L"Pipeline");
        return false;
    }

    std::vector<int> targets = inputs_.keepEditionIndices.empty()
        ? [&]{ std::vector<int> v; for (auto& e : editions) v.push_back(e.index); return v; }()
        : inputs_.keepEditionIndices;

    // For each target edition: mount → apply changes → unmount(commit).
    fs::path mountDir = util::wimMountDir(scratch_);
    for (int idx : targets) {
        if (checkCancel()) return false;

        WimMount m{ installWim, idx, mountDir, true };
        if (!mountWim(m, stageProgress(Stage::MountWim, L"Mounting WIM"))) {
            log.error(L"Mount failed for index " + std::to_wstring(idx), L"Pipeline");
            return false;
        }

        // Stage 4: registry / tweak edits
        TweakContext ctx{
            mountDir,
            mountDir / L"Windows" / L"System32" / L"config" / L"SOFTWARE",
            mountDir / L"Windows" / L"System32" / L"config" / L"SYSTEM",
            mountDir / L"Users"   / L"Default"  / L"NTUSER.DAT",
        };

        const auto& tweaks = tweakCatalog();
        int applied = 0, totalTweaks = 0;
        for (const auto& change : inputs_.changes) {
            if (change.kind == ChangeKind::Tweak) ++totalTweaks;
        }
        for (const auto& change : inputs_.changes) {
            if (change.kind != ChangeKind::Tweak) continue;
            for (const auto& t : tweaks) {
                if (t.id != change.targetId) continue;
                bool ok = t.apply ? t.apply(ctx) : false;
                ++applied;
                int pct = totalTweaks ? (applied * 100 / totalTweaks) : 100;
                report(Stage::ApplyRegistry,
                       std::wstring(L"Tweak: ") + t.displayName +
                       (ok ? L"" : L" (FAILED)"),
                       pct);
                if (!ok && !change.continueOnError) {
                    unmountWim(m, false,
                               stageProgress(Stage::UnmountWim, L"Discarding"));
                    return false;
                }
                break;
            }
        }
        if (totalTweaks == 0) report(Stage::ApplyRegistry, L"No tweaks queued", 100);

        // Stage 5: DISM ops (component removal, features, capabilities, drivers, updates)
        int dismDone = 0, dismTotal = 0;
        for (const auto& c : inputs_.changes) {
            if (c.kind == ChangeKind::Component || c.kind == ChangeKind::Feature ||
                c.kind == ChangeKind::Driver    || c.kind == ChangeKind::Update)
                ++dismTotal;
        }
        for (const auto& c : inputs_.changes) {
            if (checkCancel()) return false;
            bool ok = true;
            switch (c.kind) {
                case ChangeKind::Component:
                    ok = removeProvisionedAppx(mountDir, c.targetId,
                            stageProgress(Stage::ApplyDism, L"Remove " + c.targetId));
                    break;
                case ChangeKind::Feature:
                    ok = (c.action == ChangeAction::Remove)
                        ? disableFeature(mountDir, c.targetId,
                              stageProgress(Stage::ApplyDism, L"Disable " + c.targetId))
                        : enableFeature(mountDir, c.targetId,
                              stageProgress(Stage::ApplyDism, L"Enable " + c.targetId));
                    break;
                case ChangeKind::Driver:
                    ok = addDriver(mountDir, c.payload,
                            stageProgress(Stage::ApplyDism, L"Add driver " + c.targetId));
                    break;
                case ChangeKind::Update:
                    ok = addPackage(mountDir, c.payload,
                            stageProgress(Stage::ApplyDism, L"Add update " + c.targetId));
                    break;
                default: continue;
            }
            ++dismDone;
            int pct = dismTotal ? (dismDone * 100 / dismTotal) : 100;
            report(Stage::ApplyDism, c.description, pct);
            if (!ok && !c.continueOnError) {
                unmountWim(m, false,
                           stageProgress(Stage::UnmountWim, L"Discarding"));
                return false;
            }
        }
        if (dismTotal == 0) report(Stage::ApplyDism, L"No DISM ops queued", 100);

        // Stage 6: stage third-party installers
        std::vector<std::wstring> setupCompleteExtras;
        const auto& appCat = builtinAppCatalog();
        fs::path stage = mountDir / L"Windows" / L"Setup" / L"WIDApps";
        std::error_code ec;
        fs::create_directories(stage, ec);

        int appDone = 0, appTotal = 0;
        for (const auto& c : inputs_.changes)
            if (c.kind == ChangeKind::Application) ++appTotal;
        for (const auto& c : inputs_.changes) {
            if (c.kind != ChangeKind::Application) continue;
            const AppEntry* a = findApp(appCat, c.targetId);
            if (!a) continue;
            // payload may carry a local installer path; if absent, we leave a
            // marker that requires the user to download before applying.
            if (!c.payload.empty() && fs::exists(fs::path(c.payload), ec)) {
                fs::path dst = stage / fs::path(c.payload).filename();
                fs::copy_file(c.payload, dst,
                              fs::copy_options::overwrite_existing, ec);
                std::wstring line = L"start /wait \"\" \"%SystemDrive%\\Windows\\Setup\\WIDApps\\" +
                                    dst.filename().wstring() + L"\" " + a->silentArgs;
                setupCompleteExtras.push_back(line);
            } else {
                util::Log::instance().warn(
                    L"App " + a->displayName +
                    L": no local installer supplied; download manager TODO.",
                    L"Pipeline");
            }
            ++appDone;
            int pct = appTotal ? (appDone * 100 / appTotal) : 100;
            report(Stage::StageInstallers, a->displayName, pct);
        }
        if (appTotal == 0) report(Stage::StageInstallers, L"No apps queued", 100);

        // Stage 7: write SetupComplete.cmd / autounattend.xml
        if (!inputs_.commands.writeSetupComplete(mountDir, setupCompleteExtras)) {
            log.error(L"Failed to write SetupComplete.cmd", L"Pipeline");
            unmountWim(m, false,
                       stageProgress(Stage::UnmountWim, L"Discarding"));
            return false;
        }
        UnattendOptions u = inputs_.unattend;
        for (const auto& c : inputs_.commands.list(CommandPhase::PostLogon))
            u.firstLogonCommands.push_back(c);
        writeUnattendXml(isoDir / L"sources", u);
        report(Stage::WriteScripts, L"SetupComplete.cmd + unattend written", 100);

        // Stage 8: commit + unmount
        if (!unmountWim(m, true,
                        stageProgress(Stage::UnmountWim, L"Committing"))) {
            log.error(L"Unmount/commit failed for index " + std::to_wstring(idx),
                      L"Pipeline");
            return false;
        }
    }

    // Stage 9: trim editions
    if (inputs_.trimUnselected && !inputs_.keepEditionIndices.empty()) {
        fs::path trimmed = installWim.parent_path() / L"install.trimmed.wim";
        std::error_code ec;
        fs::remove(trimmed, ec);
        bool ok = true;
        for (size_t i = 0; i < inputs_.keepEditionIndices.size(); ++i) {
            int idx = inputs_.keepEditionIndices[i];
            ok &= exportImage(installWim, idx, trimmed, L"max",
                              stageProgress(Stage::TrimEditions,
                                  L"Exporting index " + std::to_wstring(idx)));
            if (!ok) break;
        }
        if (ok) {
            fs::remove(installWim, ec);
            fs::rename(trimmed, installWim, ec);
        }
        report(Stage::TrimEditions, ok ? L"Trim complete" : L"Trim failed",
               ok ? 100 : 0);
    } else {
        report(Stage::TrimEditions, L"Skipped", 100);
    }

    // Stage 10: build ISO
    {
        IsoBuildOptions bo;
        bo.sourceDir = isoDir;
        bo.destIso   = inputs_.outputIso;
        bo.volumeLabel = L"WID_CUSTOM";
        if (!buildIso(bo, stageProgress(Stage::BuildIso, L"oscdimg"))) {
            log.error(L"oscdimg failed", L"Pipeline");
            return false;
        }
    }

    // Stage 11: verify
    {
        std::error_code ec;
        auto sz = fs::file_size(inputs_.outputIso, ec);
        report(Stage::Verify,
               L"Output: " + std::to_wstring(sz / (1024 * 1024)) + L" MB", 100);
    }

    log.info(L"Pipeline finished: " + inputs_.outputIso.wstring(), L"Pipeline");
    return true;
}

} // namespace wid::core
