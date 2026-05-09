# Journey — the offline-hive saga

A chronological record of how long we ground on a single bug and how
the mood went. Kept honest so future-us remembers where the rabbit
holes are.

## 2026-05-09 — the day we lost to RegLoadKey

**12:08** — Launched the rebuilt `WIDUtility.exe` to retry the pipeline
after yesterday's `RegLoadKey` stale-load fix (commit `c19a885`). User
optimistic. Build still failed. Log showed `RegLoadKey error 87` on
the SOFTWARE hive — different failure mode than yesterday.

**12:30** — First hypothesis: DISM left the hive read-only on the
mount, so `RegLoadKey` choked. Added a stage-to-local-temp wrapper
inside `OfflineHive` (commit `f41922c`). Theory: bypass the WimFsf
projection by copying to a real NTFS path. Built, ran. **Same error
87**, this time on the staged copy. Hypothesis dead.

**12:50** — Second hypothesis: Win11 25H2 hives need the newer
`RegLoadAppKey` API path. Added it as a fallback, plus a `reg.exe`
shell-out as a last resort (commits `a2fba31`, `c4dbbc2`). Built, ran.
**`RegLoadAppKey` returned ERROR_BADDB (1009).** New error, same
outcome: dead.

**13:15** — Third hypothesis: hive transaction logs (`.LOG1` /
`.LOG2`) were inconsistent with the primary on a fresh extraction.
Stopped copying logs alongside. **Same errors.** Started copying logs
again, since the diagnostic confirmed the primary was clean. **Same
errors.**

**13:30** — Added diagnostic to dump the hive header (`regf` signature,
sequence numbers, version) to confirm the file wasn't corrupt
(commit `69240cd`). Result: `sig=regf, seq1==seq2=293, dirty=no,
ver=1.5`. **Hive is structurally pristine.** All three loader paths
still rejected it.

**14:00** — Added a second diagnostic: walk the hbin chain end-to-end
to detect WimFsf truncation (commits `812bc66`, `e105469`). Result:
**17,798 valid hbins covering 71.9 MB of a 72 MB file**, with the
last ~92 KB being trailing padding. The bytes are intact. The OS
loader is rejecting for non-content reasons (Win11 added registry-
hive integrity checks tied to the running OS environment, and a
foreign hive — even a perfectly-formed one — fails them).

**~14:15** — User hit the wall:

> "I'm just saying, I'm getting fed up of these build errors. What do
> you think? I am getting fed up."

Time spent on this single bug: **~2 hours of build/run/diagnose
loops**, ~6 commits of iterative diagnostics, all converging on the
same conclusion that should have been the first one: the OS loader is
not the right tool for this job. NTLite hits the same wall — modern
Win11 install.wim hives are not designed to be edited offline by
third-party code via the registry API.

**14:30** — Pivot decided. Skip offline-hive editing entirely. Generate
a `.reg` file, drop it in `<mount>\Windows\Setup\Scripts\` along with
a `SetupComplete.cmd` that runs `reg import` against it. The tweaks
land at install time (during the final "Getting ready" screen, after
OOBE, before first logon, in SYSTEM context). User wants the cmd
window visible so they can watch the configuration happen live.

## Lessons

1. **When three independent code paths reject the same input, stop
   tuning the inputs and start questioning the approach.** We spent
   90 minutes on RegLoadKey / RegLoadAppKey / reg.exe variations
   before considering that the problem class itself was wrong.
2. **Microsoft's APIs are the only ones that can edit a hive while
   the OS is running. Everything else is byte-level file editing.**
   Either accept that and use a library (hivex), or sidestep by
   editing at install time when the OS *is* running and the API works
   normally.
3. **Future Windows versions will keep tightening this.** Build
   pipelines that depend on offline-hive editing have an expiring
   shelf life. SetupComplete.cmd is the install-time path Microsoft
   intends customers to use; it's stable.
