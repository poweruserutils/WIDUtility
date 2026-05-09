#include "core/Pipeline.h"

#include "core/Apps.h"
#include "core/Hive.h"
#include "core/Tweaks.h"
#include "util/Log.h"
#include "util/Paths.h"

namespace wid::core {

// ---------------------------------------------------------------------------
// Pipeline (lean rewrite, 2026-05-09)
//
// Six straight-line steps. Every step logs [INF] before and [INF]/[ERR]
// after. No silent return paths: if the function returns false, the line
// above it in the log says why. Multi-edition trim, the DISM ops queue,
// and the unattend writer are intentionally absent — they were unused
// surface area that hid silent failures. They can come back as separate
// steps when a UI surfaces them.

Pipeline::Pipeline(PipelineInputs inputs) : inputs_(std::move(inputs)) {}

namespace {

// Map our flat steps to the legacy Stage enum so the GUI's progress
// dialog (which keys off Stage) keeps working.
void emit(const PipelineProgress& progress, Stage st,
          const std::wstring& label, int pct, int overall) {
    if (!progress) return;
    StageReport r{};
    r.stage = st;
    r.label = label;
    r.percent = pct;
    r.overallPercent = overall;
    progress(r);
}

} // namespace

bool Pipeline::run(const PipelineProgress& progress) {
    scratch_ = util::createScratchRoot();
    auto& log = util::Log::instance();
    log.info(L"Pipeline scratch root: " + scratch_.wstring(), L"Pipeline");

    // End-of-run scratch cleanup (success OR failure). The scratch can be
    // ~8 GB after a Win11 ISO+WIM extract, so leaving it around between
    // runs is a quick way to fill the user's disk. cleanScratchRoot does
    // a best-effort dism /Discard against any leftover mount before
    // remove_all so we don't silently fail on a half-mounted state.
    struct ScratchGuard {
        fs::path root;
        ~ScratchGuard() {
            if (!root.empty()) {
                util::Log::instance().info(
                    L"Cleaning scratch: " + root.wstring(), L"Pipeline");
                util::cleanScratchRoot(root);
            }
        }
    } scratchGuard{ scratch_ };

    auto fail = [&](const std::wstring& step, const std::wstring& why) {
        log.error(step + L": " + why, L"Pipeline");
        return false;
    };

    // Per-step progress callback that just forwards to the GUI under a
    // single Stage. Sub-percent goes 0..100 within each step.
    auto stepProgress = [&](Stage st, const std::wstring& label,
                            int overallStart, int overallEnd) {
        return [&, st, label, overallStart, overallEnd]
               (std::wstring_view sub, int pct) {
            std::wstring combined = label;
            if (!sub.empty()) combined += L" — " + std::wstring(sub);
            int overall = overallStart +
                          (overallEnd - overallStart) * pct / 100;
            emit(progress, st, combined, pct, overall);
        };
    };

    // ----- Step 1: extract ISO ----------------------------------------------
    log.info(L"Step 1/6: extracting ISO " + inputs_.sourceIso.wstring(),
             L"Pipeline");
    fs::path isoDir = util::isoExtractDir(scratch_);
    {
        IsoExtractOptions eo{ inputs_.sourceIso, isoDir };
        if (!extractIso(eo, stepProgress(Stage::ExtractIso,
                                         L"Extracting ISO", 0, 15))) {
            return fail(L"Step 1", L"extractIso returned false");
        }
    }
    log.info(L"Step 1/6: ok", L"Pipeline");
    if (cancel_.load()) return fail(L"Step 1", L"cancelled");

    // ----- Step 2: pick edition + mount -------------------------------------
    fs::path installWim = isoDir / L"sources" / L"install.wim";
    if (!fs::exists(installWim))
        installWim = isoDir / L"sources" / L"install.esd";
    if (!fs::exists(installWim))
        return fail(L"Step 2", L"install.wim/esd not found in extracted ISO");

    log.info(L"Step 2/6: inspecting " + installWim.wstring(), L"Pipeline");
    auto editions = getWimInfo(installWim,
                               stepProgress(Stage::InspectWim,
                                            L"Inspecting WIM", 15, 17));
    if (editions.empty())
        return fail(L"Step 2", L"no editions detected in install.wim");

    int targetIdx = inputs_.keepEditionIndices.empty()
                  ? editions.front().index
                  : inputs_.keepEditionIndices.front();
    log.info(L"Step 2/6: mounting edition index " +
             std::to_wstring(targetIdx), L"Pipeline");

    fs::path mountDir = util::wimMountDir(scratch_);
    WimMount mount{ installWim, targetIdx, mountDir, true };
    if (!mountWim(mount, stepProgress(Stage::MountWim,
                                      L"Mounting WIM", 17, 35))) {
        return fail(L"Step 2",
                    L"mount failed for index " + std::to_wstring(targetIdx));
    }
    log.info(L"Step 2/6: ok (mounted at " + mountDir.wstring() + L")",
             L"Pipeline");

    // RAII so any early-return from here unmounts cleanly via /Discard.
    bool committed = false;
    auto cleanup = [&]() {
        if (!committed) {
            log.warn(L"Discarding mount due to earlier failure", L"Pipeline");
            unmountWim(mount, false,
                       stepProgress(Stage::UnmountWim, L"Discarding", 90, 95));
        }
    };

    // ----- Step 3: apply tweaks ---------------------------------------------
    log.info(L"Step 3/6: applying tweaks", L"Pipeline");
    RegScript regScript;
    std::vector<std::wstring> setupExtras;
    TweakContext ctx{
        mountDir,
        mountDir / L"Windows" / L"System32" / L"config" / L"SOFTWARE",
        mountDir / L"Windows" / L"System32" / L"config" / L"SYSTEM",
        mountDir / L"Users"   / L"Default"  / L"NTUSER.DAT",
        &regScript,
        &setupExtras,
    };

    const auto& tweakCat = tweakCatalog();
    int tweakTotal = 0;
    for (const auto& c : inputs_.changes)
        if (c.kind == ChangeKind::Tweak) ++tweakTotal;

    int tweakDone = 0, tweakOk = 0, tweakFail = 0;
    for (const auto& change : inputs_.changes) {
        if (change.kind != ChangeKind::Tweak) continue;
        const TweakEntry* match = nullptr;
        for (const auto& t : tweakCat)
            if (t.id == change.targetId) { match = &t; break; }
        if (!match) {
            log.warn(L"Step 3/6: unknown tweak id " + change.targetId,
                     L"Pipeline");
            continue;
        }

        bool ok = match->apply ? match->apply(ctx) : false;
        ++tweakDone;
        (ok ? tweakOk : tweakFail)++;
        log.info(L"  tweak: " + match->displayName +
                 (ok ? L" -> ok" : L" -> FAILED"), L"Pipeline");

        emit(progress, Stage::ApplyRegistry,
             L"Tweak: " + match->displayName + (ok ? L"" : L" (FAILED)"),
             tweakTotal ? tweakDone * 100 / tweakTotal : 100,
             35 + (tweakTotal ? 10 * tweakDone / tweakTotal : 10));

        if (!ok && !change.continueOnError) {
            cleanup();
            return fail(L"Step 3",
                        L"tweak '" + match->displayName + L"' failed");
        }
    }
    log.info(L"Step 3/6: ok (" + std::to_wstring(tweakOk) + L" applied, " +
             std::to_wstring(tweakFail) + L" failed-but-continued, " +
             std::to_wstring(tweakTotal) + L" queued)", L"Pipeline");

    // ----- Step 4: write WID-tweaks.reg + SetupComplete.cmd -----------------
    log.info(L"Step 4/6: writing setup scripts (" +
             std::to_wstring(setupExtras.size()) + L" tweak cmd lines, " +
             std::wstring(regScript.empty() ? L"no" : L"some") +
             L" reg entries)", L"Pipeline");
    if (!regScript.empty()) {
        if (!writeRegScriptFile(ctx)) {
            cleanup();
            return fail(L"Step 4", L"writeRegScriptFile failed");
        }
        // reg import runs first, before any tweak-cmd lines (so apps
        // installed via cmd see the tweaks already applied).
        setupExtras.insert(setupExtras.begin(), regImportSetupCompleteLine());
    }
    if (!inputs_.commands.writeSetupComplete(mountDir, setupExtras)) {
        cleanup();
        return fail(L"Step 4", L"CommandSet::writeSetupComplete failed");
    }
    log.info(L"Step 4/6: ok", L"Pipeline");
    emit(progress, Stage::WriteScripts, L"Setup scripts written", 100, 50);

    // ----- Step 5: unmount /Commit ------------------------------------------
    log.info(L"Step 5/6: committing + unmounting WIM", L"Pipeline");
    if (!unmountWim(mount, true,
                    stepProgress(Stage::UnmountWim,
                                 L"Committing WIM", 50, 80))) {
        // unmount-with-commit failed; nothing more to do, the mount is
        // either already cleaned up by dism or in a bad state we can't fix.
        return fail(L"Step 5", L"unmountWim(commit) failed");
    }
    committed = true;
    log.info(L"Step 5/6: ok", L"Pipeline");

    // ----- Step 6: oscdimg → output ISO -------------------------------------
    log.info(L"Step 6/6: building ISO with oscdimg; source=" +
             isoDir.wstring() + L", dest=" + inputs_.outputIso.wstring(),
             L"Pipeline");
    {
        IsoBuildOptions bo;
        bo.sourceDir   = isoDir;
        bo.destIso     = inputs_.outputIso;
        bo.volumeLabel = L"WID_CUSTOM";
        if (!buildIso(bo, stepProgress(Stage::BuildIso,
                                       L"Building ISO", 80, 99))) {
            return fail(L"Step 6", L"buildIso (oscdimg) failed");
        }
    }
    log.info(L"Step 6/6: ok", L"Pipeline");

    // Final size report.
    std::error_code ec;
    auto sz = fs::file_size(inputs_.outputIso, ec);
    log.info(L"Pipeline finished: " + inputs_.outputIso.wstring() +
             L" (" + std::to_wstring(sz / (1024 * 1024)) + L" MB)",
             L"Pipeline");
    emit(progress, Stage::Verify,
         L"Output: " + std::to_wstring(sz / (1024 * 1024)) + L" MB",
         100, 100);
    return true;
}

} // namespace wid::core
