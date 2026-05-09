# Build-fail fixes

Running log of build pipeline failures and what we did about them.
Newest at the top.

## 2026-05-09 — `RegLoadKey` rejects Win11 25H2 SOFTWARE hive

### Symptom

Pipeline mounts `install.wim` successfully, then `OfflineHive`
construction fails immediately on the SOFTWARE hive:

```
[ERR] Hive: RegLoadKey failed for ...\config\SOFTWARE (error 87, ...)
```

DISM unmounts cleanly afterward (no half-mounted state), but every
tweak that depends on offline registry editing is skipped, and the
build is marked failed.

### Root cause

Modern Win11 install.wim hives are byte-perfect (`regf` header valid,
sequence numbers match, `hbin` chain intact end-to-end), but the OS
registry loader rejects them when `RegLoadKey`/`RegLoadAppKey`/
`reg.exe load` are called from a third-party process. This is not a
file-integrity issue — it's a Win11-era integrity / environment check
inside `ntoskrnl.exe`'s configuration manager that doesn't pass when
loading a foreign hive.

The rejection is API-wide. There is no flag to disable it.

### Failed approaches (all kept committed for the historical record)

| Commit    | Approach                                              | Result            |
|-----------|-------------------------------------------------------|-------------------|
| `c19a885` | Defensive `RegUnLoadKey` before load (stale-load fix) | Still failed      |
| `f41922c` | Stage hive to local temp dir before load              | Same error 87     |
| `a2fba31` | `RegLoadAppKey` fallback                              | `ERROR_BADDB`     |
| `c4dbbc2` | Drop `.LOG1`/`.LOG2` companions (theory: log mismatch) | Same errors       |
| `44ee477` | Restore log companions + `reg.exe load` fallback      | All three failed  |
| `69240cd` | Header diagnostic (sig/seq/version)                   | Confirms hive valid |
| `e105469` | Hbin-chain walker (detect WimFsf truncation)          | Confirms bytes intact |

### Fix (in progress)

Pivot to `SetupComplete.cmd` + `.reg` import. Tweaks are no longer
applied offline; they're written into the image as a text `.reg` file
that Windows itself imports during the final "Getting ready" screen
of install (after OOBE completes, before first logon, in SYSTEM
context).

Mechanism:

1. During pipeline build, generate `<mount>\Windows\Setup\Scripts\
   WID-tweaks.reg` from the user's tweak selections.
2. Generate `<mount>\Windows\Setup\Scripts\SetupComplete.cmd` that
   runs `reg import "%~dp0WID-tweaks.reg"`.
3. cmd window is visible to the end user during install — they see
   the configuration scroll past and complete.
4. Commit + unmount WIM normally. No `RegLoadKey` involvement during
   build. No more loader rejections.

### Tweaks unaffected by the pivot

- LabConfig bypasses (TPM/SecureBoot/RAM/CPU) target the **boot WIM's**
  SYSTEM hive, not the install WIM's SOFTWARE hive. Boot WIM is much
  smaller and may load fine — to be tested. If it also fails, fall back
  to `appraiserres.dll` substitution (documented and reliable).
- File-system-level tweaks (e.g. `sethc.exe` swap, Start menu shortcut
  drops) don't touch the registry and continue to work as planned.

### Known gotcha for the new path

`SetupComplete.cmd` is documented to be **disabled when the install
uses an OEM product key**, except on Enterprise/Server editions. For
retail keys and generic / no-key installs it runs normally. Power
users hitting this tool generally aren't using OEM keys (those are
machine-locked anyway), but if it becomes a real problem there are
documented workarounds (oobe.cmd path, scheduled-task triggered on
first boot, etc.).
