# Windows ISO Creator — Plan

A free, open-source, no-paywall power-user alternative to NTLite. Every feature
unlocked, no license tiers, no artificial guardrails.

## Scope (locked)

**ISO in, ISO out.** The only supported workflow is: user picks a source
Windows installation ISO, edits, and gets a customized ISO back. No other
input modes (no loose WIM, no folder, no running-OS modification, no
servicing of an installed Windows). No CLI mode.

## Critical Operating Principle

**All modifications are applied to the ISO/WIM offline. Nothing is ever applied
to the user's running Windows install.** The workflow is always:

1. Load source ISO (or extracted folder).
2. Extract / mount `sources\install.wim` (or `install.esd`) to a scratch dir.
3. Modify the offline image:
   - DISM `/Image:<mount>` operations for packages, features, drivers, capabilities.
   - Offline registry edits via `RegLoadKey` against the mounted hives
     (`<mount>\Windows\System32\config\SOFTWARE`, `SYSTEM`, `DEFAULT`, and the
     default user `NTUSER.DAT`).
   - File-level injection/deletion inside the mounted tree.
4. Commit + unmount the WIM.
5. Rebuild bootable UEFI+BIOS ISO with `oscdimg` (Windows ADK).

The host OS is never modified.

## Stack

- **Language:** C++ (C++20).
- **Build:** CMake + Visual Studio Build Tools 2022 (MSVC).
- **GUI:** TBD — candidates: Qt 6, Dear ImGui, wxWidgets, or Win32 + common
  controls. Decision pending. Target look/feel: **NTLite-style** (left-hand
  navigation pane, right-hand detail/checkbox lists).
- **No CLI mode.** GUI only.
- **Backend tooling shelled out to:**
  - `dism.exe` (in-box on Windows)
  - `oscdimg.exe` (from Windows ADK — Deployment Tools)
  - Direct Win32 API: `RegLoadKeyW`, `RegUnLoadKeyW`, `RegSaveKeyExW` for
    offline hive editing.
- **Target images:** Windows 10 and Windows 11, x64 primarily (ARM64 later).

## Feature Set

### 1. Application Management (no NTLite-style limits)

- Add, remove, uninstall **any** app or component in the image — including ones
  NTLite refuses to touch (Edge, Defender, Store, Copilot, WinRE, etc.).
- Operates against:
  - Provisioned AppX/MSIX packages (`Get-ProvisionedAppxPackage` equivalent via
    DISM `/Get-ProvisionedAppxPackages`).
  - Optional features (`/Get-Features`, `/Disable-Feature`, `/Enable-Feature`).
  - Capabilities (`/Get-Capabilities`, `/Remove-Capability`).
  - Inbox components removable only by direct file/registry surgery (the
    "things NTLite won't let you remove" tier).

### 2. Pre-installed Third-Party Applications

Apps must be **already installed** by the time the user reaches the
WinDeploy / "Getting ready" / OOBE phase — not deferred to first user logon.

Implementation approach:
- Stage installers into `<mount>\Windows\Setup\Scripts\` or a custom dir.
- Use `SetupComplete.cmd` (runs during the Specialize/AuditSystem-equivalent
  late-setup phase, in SYSTEM context, before user logon) to silently install
  each selected app with its known silent-install switches.
- Optionally use `unattend.xml` `FirstLogonCommands` for items that genuinely
  need a user context.

**Initial built-in app catalog (checkbox list in GUI):**

1. Google Antigravity (Google's agentic IDE)
2. Brave
3. Microsoft Edge (re-add / update if user removed it)
4. Visual Studio Code
5. Windsurf
6. Cursor
7. Claude (Claude desktop app)

The catalog must be **user-extensible** — user can add custom entries by
pointing at an installer path/URL and providing silent-install switches.

### 3. Remove UAC

Offline edits to `SOFTWARE` hive under
`Microsoft\Windows\CurrentVersion\Policies\System`:
- `EnableLUA = 0`
- `ConsentPromptBehaviorAdmin = 0`
- `PromptOnSecureDesktop = 0`
- (and related values — final list TBD during implementation)

### 4. Bypass Windows 11 Install Requirements

Patch the image so Setup does not enforce:
- TPM 2.0
- Secure Boot
- RAM minimum (4 GB)
- CPU compatibility

Approaches (combined for robustness):
- Inject `LabConfig` keys into the boot WIM's offline `SYSTEM` hive
  (`HKLM\SYSTEM\Setup\LabConfig` → `BypassTPMCheck`, `BypassSecureBootCheck`,
  `BypassRAMCheck`, `BypassCPUCheck`, `BypassStorageCheck` all = 1).
- Optionally replace/neuter `appraiserres.dll` in `sources\` with the Win10
  version or a zero-byte stub.
- Set `HKLM\SYSTEM\Setup\MoSetup\AllowUpgradesWithUnsupportedTPMOrCPU = 1`.

### 5. SYSTEM Account Access

Expose interactive use of `NT AUTHORITY\SYSTEM`. Options to offer (user picks
which, all are toggleable tweaks):
- Enable the built-in `Administrator` account + auto-elevate.
- Sticky Keys / sethc.exe swap (see Tweaks below) — gives SYSTEM cmd at lock
  screen.
- Schedule a SYSTEM-context task that launches an interactive shell on demand
  (via `psexec`-style technique baked in, or a service).
- Create a `PsExec`-equivalent helper pre-staged in the image.

Final mechanism mix TBD — at minimum the sethc swap is in.

### 6. Tweaks List (NTLite-style toggle list)

Must include, at minimum:
- **sethc.exe swap** — replace `<mount>\Windows\System32\sethc.exe` with
  `cmd.exe` (or rename cmd → sethc). Press Shift × 5 at lock screen → SYSTEM
  cmd. Classic LPE / password-recovery trick.
- **Verbose system messages enabled by default**
  `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System\VerboseStatus = 1`
- **Start menu: "Reboot to BIOS/UEFI Settings"** — installs a Start-menu
  shortcut that runs `shutdown.exe /r /fw /t 0`, rebooting directly into the
  firmware setup utility. Universal, works on any UEFI system. Optional
  sub-toggle: also pin to Start by default via `LayoutModification.json` /
  `LayoutModification.xml`.
  - **Accidental-click guard (mandatory):** the shortcut does NOT call
    `shutdown` directly. It launches a small confirmation helper that shows
    two sequential dialogs:
    1. First prompt: **"Do you want to do this?"** (OK / Cancel)
    2. After OK, second prompt (double-check): **"Do you really want to do
       this?"** (OK / Cancel)
    Only after both OK clicks does the helper invoke
    `shutdown.exe /r /fw /t 0`. Cancel at either step aborts.
- **Start menu: "Reboot to Boot Device…"** — installs a Start-menu shortcut
  that launches a small helper app (shipped as part of WID utility, staged
  into `<mount>\Windows\System32\` or `Program Files\WID\`). The helper:
  1. Enumerates UEFI boot entries via `GetFirmwareEnvironmentVariableW` on
     `BootOrder` + each `Boot####` entry.
  2. Shows a dialog listing them (USB sticks, network, disks, etc.).
  3. On selection, shows the **mandatory accidental-click guard** — two
     sequential confirmation dialogs:
     a. First prompt: **"Do you want to do this?"** (OK / Cancel)
     b. After OK, second prompt (double-check): **"Do you really want to do
        this?"** (OK / Cancel)
     Cancel at either step aborts and leaves `BootNext` unset.
  4. After both OKs, sets the UEFI `BootNext` variable via
     `SetFirmwareEnvironmentVariableW` and calls `ExitWindowsEx` to reboot.
  - **`BootNext` semantics (important):** strictly one-time. Firmware deletes
    the variable as soon as it reads it on next boot. Permanent `BootOrder` is
    never modified. Identical behaviour to the OEM F9/F12 one-time picker.
  - Helper requires admin + `SeSystemEnvironmentPrivilege` to read/write EFI
    vars; shortcut launches elevated.
- **NOT feasible / explicitly out of scope:** invoking the OEM-specific
  pre-boot one-time boot menu (HP F9, Dell F12, etc.) directly from the OS.
  No standardized UEFI/ACPI mechanism exists. The `BootNext` approach above
  is the supported substitute.
- (More tweaks to be added — full list TBD: telemetry off, Cortana off, Recall
  off, Copilot off, OneDrive off, ads off, lockscreen tips off, etc.)

### 7. Commands Window (NTLite-style)

Two scriptable hook lists, **both unlimited length**, dynamically
add/remove/reorder in the GUI:

- **Pre-logon commands** — written into `SetupComplete.cmd` (and/or
  `unattend.xml` synchronous commands). Run in SYSTEM context before the first
  user logon.
- **Post-logon commands** — written into `unattend.xml` `FirstLogonCommands`
  and/or a `RunOnce` entry in the default user hive. Run after first user
  logon.

Each command entry stores: order index, command line, optional description,
optional "continue on error" flag.

## Edit Model & Progress Reporting

### Two-phase edit model (NTLite-style)

1. **Edit phase** — user toggles components, adds apps, edits tweaks/commands.
   No image modification happens yet. Every change is enqueued into a
   **Pending Changes** side pane:
   - Each entry: human-readable description, category, originating panel,
     timestamp, undo button.
   - User can review, reorder (where order matters), or remove any pending
     entry before committing.
   - Pending list is serializable to a **profile / preset file** (JSON or
     TOML) that can be saved, shared, and re-loaded — turns a configuration
     into a reproducible build recipe.

2. **Apply phase** — user clicks **Apply**. Pending queue is executed against
   the offline image. Nothing is destructive until this point.

### Apply-phase progress UI

Modal progress window containing:
- **Overall progress bar** — % of queued operations completed.
- **Current operation label** — e.g.
  *"Mounting install.wim (index 3 — Pro)…"*,
  *"Removing provisioned package Microsoft.BingNews…"*,
  *"Writing SetupComplete.cmd…"*,
  *"Rebuilding ISO with oscdimg…"*.
- **Sub-progress bar** for the current op. Where DISM emits progress on
  stdout, parse it; otherwise indeterminate spinner.
- **Live log pane** (expandable / collapsible). Every DISM, oscdimg, and
  helper invocation logged with full command line, stdout, stderr, exit
  code. Scrollable, copyable, "Save log…" button writes to disk.
- **Rough ETA** — based on remaining operation count and rolling average.
- **Cancel button** — best-effort cancel: finish the current atomic op,
  then unmount the WIM with `/Discard` so we never leave a half-mounted
  image on the user's machine.
- **Status bar** at the bottom of the main window mirrors the current
  operation label when the modal is dismissed / minimized.

### Major reported stages (always shown)

1. Extract ISO contents to scratch dir
2. Inspect WIM (enumerate editions, components, features)
3. Mount WIM (per selected edition)
4. Apply offline registry edits (UAC, VerboseStatus, LabConfig bypasses, etc.)
5. Apply DISM operations (per package / feature / capability / driver)
6. Stage third-party installers
7. Write `SetupComplete.cmd` and `unattend.xml`
8. Commit + unmount WIM
9. Export-trim editions (if enabled)
10. Rebuild bootable ISO with `oscdimg`
11. Verify output ISO (size, structure, optional checksum)

## Edition Selection & Trimming

### Detection

On ISO load, run `dism /Get-WimInfo /WimFile:<sources\install.wim>` to
enumerate all editions present. Each edition is a WIM **index** (e.g.
*1: Home, 2: Home N, 3: Pro, 4: Pro N, 5: Education, 6: Enterprise*…).

### GUI: Editions panel

- Checkbox list of every detected edition with name, index, architecture,
  build number, and size.
- User can select **one or multiple** editions to keep (e.g. Pro + Edu).
- All subsequent component/app/tweak edits apply to **every selected
  edition**. Implementation strategy: mount and modify each in turn. If the
  shared component baseline allows, optimize by editing once and using
  `Export-Image` to propagate; final approach TBD during implementation.

### Trimming unselected editions

After Apply, the build offers:
- **"Trim unselected editions from ISO"** checkbox — default **ON**, with a
  tooltip explaining it significantly shrinks the output ISO.
- Implementation:
  `dism /Export-Image /SourceImageFile:<old install.wim>
   /SourceIndex:<n> /DestinationImageFile:<new install.wim>
   /Compress:max` for each **kept** edition into a fresh WIM, then replace
  the original `install.wim`. Unkept indices vanish.
- If trim is unchecked: original `install.wim` is preserved with all
  editions intact, but our edits are applied only to the selected
  edition(s). Unselected editions remain pristine in the output.

## GUI Layout (NTLite-inspired)

- **Left nav pane** with sections:
  - Image (load ISO / mount WIM / edition picker)
  - Components (removable Windows components tree)
  - Features (optional features on/off)
  - Applications (third-party app checkbox catalog — section 2)
  - Tweaks (toggle list — section 6)
  - Unattended (OOBE answers, accounts, region, EULA, MS-account skip)
  - Commands (pre-logon + post-logon lists — section 7)
  - Drivers / Updates (integration)
  - Apply (review + build new ISO)
- **Right pane:** detail view for the selected section, with checkboxes /
  toggles / editable lists as appropriate.

## Build / Project Structure (planned)

```
WID utility/
├── CMakeLists.txt
├── plan.md
├── src/
│   ├── main.cpp
│   ├── gui/                 # GUI layer (toolkit TBD)
│   ├── core/
│   │   ├── iso.{h,cpp}      # ISO extract + oscdimg rebuild
│   │   ├── wim.{h,cpp}      # mount/unmount/commit via DISM
│   │   ├── hive.{h,cpp}     # offline registry hive load/edit/save
│   │   ├── components.{h,cpp}
│   │   ├── apps.{h,cpp}     # third-party app catalog + installer staging
│   │   ├── tweaks.{h,cpp}
│   │   ├── commands.{h,cpp} # SetupComplete.cmd + unattend.xml writer
│   │   └── unattend.{h,cpp}
│   └── util/
└── third_party/
```

## Open Decisions

1. ~~GUI toolkit~~ — **DECIDED:** Win32 + Common Controls v6. Zero external
   deps; closest visual match to NTLite (which is WinForms over native common
   controls).
2. License (MIT vs GPLv3) — GPL discourages a closed-source fork going pay.
3. Win10 + Win11 both day-one, or Win11-first.
4. How "deep" the SYSTEM-account access feature goes (sethc only, or also
   service/scheduled-task helpers).
5. Final tweaks catalog.

## Status

Planning only. **No code written yet.** Awaiting further direction from user
before starting implementation.

## TODO — next session (2026-05-09+)

- **Build still fails after the RegLoadKey stale-load fix landed in
  c19a885.** Reproduce, then read the latest
  `Desktop\WID Utility Logs\WID-*.log` to identify which stage breaks now.
  Likely candidates to investigate first:
  1. A second tweak failing after UAC succeeds (e.g. sethc swap, LabConfig
     on the SYSTEM hive — that hive uses a different subkey name, which
     should be fine, but verify).
  2. WIM commit/unmount failing because something inside the mounted tree
     is held open (a stray file handle on the SOFTWARE hive after
     `OfflineHive` destruction).
  3. `Export-Image` (trim stage) failing on a too-large WIM.
  4. `oscdimg` failing because boot files are not where we expect on
     non-multi-arch ISOs.
- After that's green, wire the Components and Features panels to populate
  from the mounted WIM (currently they are still empty placeholders).
- Build the `WIDRebootHelper.exe` companion (double-confirm dialog +
  `BootNext` picker) and stage it during the pipeline so the
  reboot-to-UEFI / reboot-to-boot-device Start menu shortcuts work.
- App download manager: today the pipeline only stages installers when
  the user supplies a local path; URL fetching is a no-op.
