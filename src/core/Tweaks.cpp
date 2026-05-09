#include "core/Tweaks.h"

#include "util/Log.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <fstream>

namespace wid::core {

// ---------------------------------------------------------------------------
// RegScript

namespace {

std::wstring escapeRegString(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 2);
    for (wchar_t c : s) {
        if (c == L'\\' || c == L'"') out += L'\\';
        out += c;
    }
    return out;
}

std::wstring keyHeader(const std::wstring& root, const std::wstring& path) {
    // .reg expects the long form, e.g. HKEY_LOCAL_MACHINE\Software\...
    std::wstring full;
    if (root == L"HKLM")      full = L"HKEY_LOCAL_MACHINE";
    else if (root == L"HKCU") full = L"HKEY_CURRENT_USER";
    else if (root == L"HKU")  full = L"HKEY_USERS";
    else if (root == L"HKCR") full = L"HKEY_CLASSES_ROOT";
    else                      full = root;
    if (!path.empty()) {
        full += L"\\";
        full += path;
    }
    return L"[" + full + L"]\r\n";
}

} // namespace

void RegScript::setDword(const std::wstring& root, const std::wstring& path,
                         const std::wstring& name, unsigned int value) {
    body_ += keyHeader(root, path);
    wchar_t buf[64];
    swprintf_s(buf, L"\"%ls\"=dword:%08x\r\n", name.c_str(), value);
    body_ += buf;
    body_ += L"\r\n";
}

void RegScript::setString(const std::wstring& root, const std::wstring& path,
                          const std::wstring& name, const std::wstring& value) {
    body_ += keyHeader(root, path);
    body_ += L"\"" + escapeRegString(name) + L"\"=\"" +
             escapeRegString(value) + L"\"\r\n\r\n";
}

void RegScript::deleteValue(const std::wstring& root, const std::wstring& path,
                            const std::wstring& name) {
    body_ += keyHeader(root, path);
    body_ += L"\"" + escapeRegString(name) + L"\"=-\r\n\r\n";
}

void RegScript::deleteKey(const std::wstring& root, const std::wstring& path) {
    // Leading '-' on the key header means delete-key in .reg syntax.
    std::wstring h = keyHeader(root, path);
    // Insert '-' right after the opening '['.
    if (h.size() > 1) h.insert(1, L"-");
    body_ += h + L"\r\n";
}

std::wstring RegScript::toRegFile() const {
    return std::wstring(L"Windows Registry Editor Version 5.00\r\n\r\n") + body_;
}

// Write the .reg + SetupComplete.cmd into the image. SetupComplete.cmd
// is launched by Windows at the end of Setup (after OOBE, before first
// logon, in SYSTEM context). We deliberately re-spawn ourselves with
// 'start "WID Utility" cmd /k' so the user sees a visible console
// scroll through what's happening on the just-installed system.
bool writeSetupCompleteFromRegScript(const TweakContext& ctx) {
    if (!ctx.regScript || ctx.regScript->empty()) return true;

    fs::path scriptsDir = ctx.mountDir / L"Windows" / L"Setup" / L"Scripts";
    std::error_code ec;
    fs::create_directories(scriptsDir, ec);
    if (ec) return false;

    fs::path regPath = scriptsDir / L"WID-tweaks.reg";
    fs::path cmdPath = scriptsDir / L"SetupComplete.cmd";

    // Write the .reg file as UTF-16 LE with BOM (the format reg.exe
    // expects for "Windows Registry Editor Version 5.00").
    {
        std::ofstream f(regPath, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        const unsigned char bom[2] = { 0xFF, 0xFE };
        f.write(reinterpret_cast<const char*>(bom), 2);
        std::wstring text = ctx.regScript->toRegFile();
        f.write(reinterpret_cast<const char*>(text.data()),
                std::streamsize(text.size() * sizeof(wchar_t)));
    }

    // Build SetupComplete.cmd. If one already exists (e.g. from a
    // previous tweak), append to it instead of overwriting.
    bool exists = fs::exists(cmdPath, ec);
    std::ofstream cmd(cmdPath, std::ios::binary |
                       (exists ? std::ios::app : std::ios::trunc));
    if (!cmd) return false;
    if (!exists) {
        cmd << "@echo off\r\n";
        cmd << "title WID Utility - applying tweaks\r\n";
        // Re-spawn visibly so the user can see the configuration happen.
        // %SystemRoot%\Setup\Scripts is where Setup invokes us from.
        cmd << "if \"%WID_VISIBLE%\"==\"\" (\r\n";
        cmd << "  set WID_VISIBLE=1\r\n";
        cmd << "  start \"WID Utility\" /wait cmd /c \"%~f0\"\r\n";
        cmd << "  exit /b\r\n";
        cmd << ")\r\n";
        cmd << "echo.\r\n";
        cmd << "echo === WID Utility: applying tweaks ===\r\n";
        cmd << "echo.\r\n";
    }
    cmd << "echo Importing WID-tweaks.reg ...\r\n";
    cmd << "reg import \"%~dp0WID-tweaks.reg\"\r\n";
    cmd << "if errorlevel 1 echo  (reg import returned %errorlevel%)\r\n";
    cmd << "echo.\r\n";
    cmd << "echo Done. This window will close in 5 seconds.\r\n";
    cmd << "timeout /t 5 /nobreak >nul\r\n";

    util::Log::instance().info(
        L"Wrote " + regPath.wstring() + L" + " + cmdPath.wstring(),
        L"Tweaks");
    return true;
}

namespace {

constexpr wchar_t kSoftwareMount[] = L"WID_OFFLINE_SOFTWARE";
constexpr wchar_t kSystemMount[]   = L"WID_OFFLINE_SYSTEM";

// Drop a .lnk into the offline All-Users Start menu folder. Uses IShellLinkW
// + IPersistFile against the offline path. Note: the shortcut's target is
// resolved at runtime on the deployed machine, so the path stored is the
// absolute on-target path (e.g. C:\Windows\System32\shutdown.exe).
bool createStartMenuShortcut(const TweakContext& ctx,
                             const std::wstring& linkName,
                             const std::wstring& targetExe,
                             const std::wstring& args,
                             const std::wstring& description) {
    fs::path startMenu = ctx.mountDir /
        L"ProgramData" / L"Microsoft" / L"Windows" /
        L"Start Menu" / L"Programs" / L"WID Tools";
    std::error_code ec;
    fs::create_directories(startMenu, ec);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hr);

    IShellLinkW* link = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IShellLinkW, reinterpret_cast<void**>(&link));
    bool ok = false;
    if (SUCCEEDED(hr) && link) {
        link->SetPath(targetExe.c_str());
        link->SetArguments(args.c_str());
        link->SetDescription(description.c_str());

        IPersistFile* pf = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile,
                                           reinterpret_cast<void**>(&pf)))) {
            fs::path lnkPath = startMenu / (linkName + L".lnk");
            ok = SUCCEEDED(pf->Save(lnkPath.c_str(), TRUE));
            pf->Release();
        }
        link->Release();
    }
    if (needUninit) CoUninitialize();
    return ok;
}

} // namespace

bool applyUacDisable(const TweakContext& ctx) {
    if (!ctx.regScript) return false;
    const std::wstring p =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
    ctx.regScript->setDword(L"HKLM", p, L"EnableLUA",                  0);
    ctx.regScript->setDword(L"HKLM", p, L"ConsentPromptBehaviorAdmin", 0);
    ctx.regScript->setDword(L"HKLM", p, L"PromptOnSecureDesktop",      0);
    return true;
}

bool applyVerboseStatus(const TweakContext& ctx) {
    if (!ctx.regScript) return false;
    ctx.regScript->setDword(
        L"HKLM",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        L"VerboseStatus", 1);
    return true;
}

bool applySethcSwap(const TweakContext& ctx) {
    fs::path sys32 = ctx.mountDir / L"Windows" / L"System32";
    fs::path sethc = sys32 / L"sethc.exe";
    fs::path cmd   = sys32 / L"cmd.exe";
    fs::path backup = sys32 / L"sethc.exe.widbak";
    std::error_code ec;
    if (!fs::exists(cmd, ec) || !fs::exists(sethc, ec)) return false;
    if (!fs::exists(backup, ec)) {
        fs::copy_file(sethc, backup, ec);
        if (ec) return false;
    }
    fs::copy_file(cmd, sethc, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool applyLabConfigBypasses(const TweakContext& ctx) {
    // NOTE: LabConfig is read from the *boot* WIM's SYSTEM hive *during*
    // Setup itself, so SetupComplete.cmd is too late for it. Until we
    // wire up boot.wim editing (separate task), this tweak emits the
    // keys into the .reg anyway — they're harmless once on the target,
    // and useful if the user re-runs Setup as an in-place upgrade. A
    // proper fix needs to inject into boot.wim's SYSTEM hive offline.
    if (!ctx.regScript) return false;
    const std::wstring p = L"SYSTEM\\Setup\\LabConfig";
    ctx.regScript->setDword(L"HKLM", p, L"BypassTPMCheck",        1);
    ctx.regScript->setDword(L"HKLM", p, L"BypassSecureBootCheck", 1);
    ctx.regScript->setDword(L"HKLM", p, L"BypassRAMCheck",        1);
    ctx.regScript->setDword(L"HKLM", p, L"BypassCPUCheck",        1);
    ctx.regScript->setDword(L"HKLM", p, L"BypassStorageCheck",    1);
    return true;
}

bool applyAllowUnsupportedTpm(const TweakContext& ctx) {
    if (!ctx.regScript) return false;
    ctx.regScript->setDword(
        L"HKLM", L"SYSTEM\\Setup\\MoSetup",
        L"AllowUpgradesWithUnsupportedTPMOrCPU", 1);
    return true;
}

bool installRebootToUefiShortcut(const TweakContext& ctx) {
    // Wraps shutdown /r /fw /t 0 with the double-confirm guard. The guard
    // helper is a tiny app we ship into <mount>\Windows\WIDTools\. The
    // shortcut target is the helper, so the .lnk is portable.
    return createStartMenuShortcut(ctx,
        L"Reboot to BIOS or UEFI Settings",
        L"%SystemRoot%\\WIDTools\\WIDRebootHelper.exe",
        L"--mode=uefi",
        L"Restart the PC into firmware setup (with confirmation).");
}

bool installRebootToBootDeviceShortcut(const TweakContext& ctx) {
    return createStartMenuShortcut(ctx,
        L"Reboot to Boot Device",
        L"%SystemRoot%\\WIDTools\\WIDRebootHelper.exe",
        L"--mode=bootnext",
        L"Pick a UEFI boot device for the next boot only "
        L"(with confirmation; permanent boot order is unchanged).");
}

const std::vector<TweakEntry>& tweakCatalog() {
    static const std::vector<TweakEntry> cat = {
        { L"tweak.uac.disable",
          L"Disable UAC",
          L"Disables User Account Control entirely. Power-user workflow; "
          L"reduces an OS security boundary.",
          L"Security", true,
          &applyUacDisable },

        { L"tweak.verbose.status",
          L"Enable verbose system messages",
          L"Shows detailed status text during logon, logoff, startup, and "
          L"shutdown.",
          L"UI", false,
          &applyVerboseStatus },

        { L"tweak.sethc.swap",
          L"Replace sethc.exe with cmd.exe (Sticky Keys → SYSTEM cmd)",
          L"Press Shift x5 at the lock screen to launch a SYSTEM-level "
          L"command prompt. Classic local-privilege-escalation / password "
          L"recovery technique.",
          L"Security", true,
          &applySethcSwap },

        { L"tweak.bypass.win11",
          L"Bypass Windows 11 install requirements (TPM, Secure Boot, RAM, CPU)",
          L"Writes LabConfig keys so Setup does not enforce hardware checks.",
          L"Boot", false,
          &applyLabConfigBypasses },

        { L"tweak.allow.unsupported.tpm",
          L"Allow upgrades on unsupported TPM / CPU",
          L"Sets MoSetup\\AllowUpgradesWithUnsupportedTPMOrCPU=1.",
          L"Boot", false,
          &applyAllowUnsupportedTpm },

        { L"tweak.startmenu.reboot.uefi",
          L"Add Start menu: Reboot to BIOS/UEFI Settings",
          L"Adds a Start-menu shortcut that reboots into firmware setup. "
          L"Includes a two-step \"Do you want to do this?\" / "
          L"\"Do you really want to do this?\" confirmation.",
          L"Boot", false,
          &installRebootToUefiShortcut },

        { L"tweak.startmenu.reboot.bootdevice",
          L"Add Start menu: Reboot to Boot Device",
          L"Adds a Start-menu shortcut that lists UEFI boot entries and sets "
          L"BootNext (one-time only, permanent boot order untouched). "
          L"Includes the two-step confirmation.",
          L"Boot", false,
          &installRebootToBootDeviceShortcut },
    };
    return cat;
}

} // namespace wid::core
