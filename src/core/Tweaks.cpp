#include "core/Tweaks.h"

#include "util/Log.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace wid::core {

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
    OfflineHive sw(ctx.softwareHive, kSoftwareMount);
    if (!sw.ok()) return false;
    const std::wstring p = L"Microsoft\\Windows\\CurrentVersion\\Policies\\System";
    bool a = sw.setDword(p, L"EnableLUA", 0);
    bool b = sw.setDword(p, L"ConsentPromptBehaviorAdmin", 0);
    bool c = sw.setDword(p, L"PromptOnSecureDesktop", 0);
    return a && b && c;
}

bool applyVerboseStatus(const TweakContext& ctx) {
    OfflineHive sw(ctx.softwareHive, kSoftwareMount);
    if (!sw.ok()) return false;
    return sw.setDword(L"Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                       L"VerboseStatus", 1);
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
    OfflineHive sysH(ctx.systemHive, kSystemMount);
    if (!sysH.ok()) return false;
    bool a = sysH.setDword(L"Setup\\LabConfig", L"BypassTPMCheck",        1);
    bool b = sysH.setDword(L"Setup\\LabConfig", L"BypassSecureBootCheck", 1);
    bool c = sysH.setDword(L"Setup\\LabConfig", L"BypassRAMCheck",        1);
    bool d = sysH.setDword(L"Setup\\LabConfig", L"BypassCPUCheck",        1);
    bool e = sysH.setDword(L"Setup\\LabConfig", L"BypassStorageCheck",    1);
    return a && b && c && d && e;
}

bool applyAllowUnsupportedTpm(const TweakContext& ctx) {
    OfflineHive sysH(ctx.systemHive, kSystemMount);
    if (!sysH.ok()) return false;
    return sysH.setDword(L"Setup\\MoSetup",
                         L"AllowUpgradesWithUnsupportedTPMOrCPU", 1);
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
