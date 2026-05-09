#include "core/Hive.h"

#include "util/Log.h"

#include <windows.h>
#include <objbase.h>

namespace wid::core {

namespace {

bool enablePrivilege(LPCWSTR name) {
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    LUID luid;
    bool ok = false;
    if (LookupPrivilegeValueW(nullptr, name, &luid)) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
        ok = (GetLastError() == ERROR_SUCCESS);
    }
    CloseHandle(token);
    return ok;
}

HKEY openOrCreate(HKEY parent, const std::wstring& path) {
    HKEY h = nullptr;
    DWORD disp = 0;
    LONG r = RegCreateKeyExW(parent, path.c_str(), 0, nullptr, 0,
                             KEY_ALL_ACCESS, nullptr, &h, &disp);
    if (r != ERROR_SUCCESS) return nullptr;
    return h;
}

} // namespace

bool OfflineHive::enableHivePrivileges() {
    bool a = enablePrivilege(SE_RESTORE_NAME);
    bool b = enablePrivilege(SE_BACKUP_NAME);
    return a && b;
}

namespace {

void stripReadOnly(const fs::path& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY))
        SetFileAttributesW(p.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
}

// Hive companion log files we copy alongside the primary file.
const wchar_t* const kHiveCompanions[] = {
    L".LOG", L".LOG1", L".LOG2", L".blf", L".regtrans-ms",
};

fs::path makeStageDir() {
    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) return {};
    GUID g{};
    CoCreateGuid(&g);
    wchar_t guidStr[64];
    swprintf_s(guidStr, L"WIDHive_%08lX%04hX%04hX",
               g.Data1, g.Data2, g.Data3);
    fs::path d = fs::path(tmp) / guidStr;
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

} // namespace

OfflineHive::OfflineHive(const fs::path& hiveFile, const std::wstring& subkey)
    : hiveFile_(hiveFile), subkey_(subkey) {
    enableHivePrivileges();

    auto& log = util::Log::instance();
    std::error_code ec;
    if (!fs::exists(hiveFile_, ec)) {
        log.error(L"Hive file does not exist: " + hiveFile_.wstring(), L"Hive");
        return;
    }

    stripReadOnly(hiveFile_);

    // Stage the hive (and its log companions) to a real local NTFS path.
    // RegLoadKey is unreliable against files projected through the DISM/WimFsf
    // filter — it returns ERROR_INVALID_PARAMETER (87) even when ordinary file
    // I/O on the same path works. We load the local copy, edit it, then copy
    // the modified bytes back into the mount in the destructor.
    stageDir_ = makeStageDir();
    if (stageDir_.empty()) {
        log.error(L"Could not create hive stage dir for " + hiveFile_.wstring(),
                  L"Hive");
        return;
    }

    stagedFile_ = stageDir_ / hiveFile_.filename();
    fs::copy_file(hiveFile_, stagedFile_,
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
        log.error(L"Hive stage copy failed: " + hiveFile_.wstring() +
                  L" -> " + stagedFile_.wstring() +
                  L" (" + std::to_wstring(ec.value()) + L")", L"Hive");
        return;
    }
    stripReadOnly(stagedFile_);

    for (const wchar_t* ext : kHiveCompanions) {
        fs::path src = hiveFile_;
        src += ext;
        if (!fs::exists(src, ec)) continue;
        fs::path dst = stagedFile_;
        dst += ext;
        std::error_code cec;
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, cec);
        if (!cec) stripReadOnly(dst);
    }

    // Defensive: a previous crashed build may have left the subkey registered.
    RegUnLoadKeyW(HKEY_LOCAL_MACHINE, subkey_.c_str());

    LONG r = RegLoadKeyW(HKEY_LOCAL_MACHINE, subkey_.c_str(),
                         stagedFile_.c_str());
    if (r != ERROR_SUCCESS) {
        auto sz = fs::file_size(stagedFile_, ec);
        DWORD attrs = GetFileAttributesW(stagedFile_.c_str());
        log.error(L"RegLoadKey failed for staged " + stagedFile_.wstring() +
                  L" (error " + std::to_wstring(r) +
                  L", size=" + std::to_wstring(sz) +
                  L", attrs=" + std::to_wstring(attrs) + L")", L"Hive");
        return;
    }
    loaded_ = true;
}

OfflineHive::~OfflineHive() {
    auto& log = util::Log::instance();
    if (loaded_) {
        LONG r = RegUnLoadKeyW(HKEY_LOCAL_MACHINE, subkey_.c_str());
        if (r != ERROR_SUCCESS) {
            log.warn(L"RegUnLoadKey failed for " + subkey_ +
                     L" (error " + std::to_wstring(r) + L")", L"Hive");
        }

        // Copy the edited hive (and any regenerated log files) back into
        // the mount, replacing the originals. RegUnLoadKey flushes/closes
        // the staged files first, so this is safe.
        if (!stagedFile_.empty()) {
            std::error_code ec;
            stripReadOnly(hiveFile_);
            fs::copy_file(stagedFile_, hiveFile_,
                          fs::copy_options::overwrite_existing, ec);
            if (ec) {
                log.error(L"Hive write-back failed: " + stagedFile_.wstring() +
                          L" -> " + hiveFile_.wstring() +
                          L" (" + std::to_wstring(ec.value()) + L")", L"Hive");
            }
            for (const wchar_t* ext : kHiveCompanions) {
                fs::path src = stagedFile_;
                src += ext;
                if (!fs::exists(src, ec)) continue;
                fs::path dst = hiveFile_;
                dst += ext;
                stripReadOnly(dst);
                std::error_code cec;
                fs::copy_file(src, dst,
                              fs::copy_options::overwrite_existing, cec);
            }
        }
    }

    if (!stageDir_.empty()) {
        std::error_code ec;
        fs::remove_all(stageDir_, ec);
    }
}

bool OfflineHive::setDword(const std::wstring& path, const std::wstring& name, DWORD value) {
    if (!loaded_) return false;
    HKEY h = openOrCreate(HKEY_LOCAL_MACHINE, subkey_ + L"\\" + path);
    if (!h) return false;
    LONG r = RegSetValueExW(h, name.c_str(), 0, REG_DWORD,
                            reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool OfflineHive::setString(const std::wstring& path, const std::wstring& name,
                            const std::wstring& value) {
    if (!loaded_) return false;
    HKEY h = openOrCreate(HKEY_LOCAL_MACHINE, subkey_ + L"\\" + path);
    if (!h) return false;
    LONG r = RegSetValueExW(h, name.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value.c_str()),
                            (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool OfflineHive::deleteValue(const std::wstring& path, const std::wstring& name) {
    if (!loaded_) return false;
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (subkey_ + L"\\" + path).c_str(),
                      0, KEY_ALL_ACCESS, &h) != ERROR_SUCCESS) return false;
    LONG r = RegDeleteValueW(h, name.c_str());
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool OfflineHive::deleteTree(const std::wstring& path) {
    if (!loaded_) return false;
    return RegDeleteTreeW(HKEY_LOCAL_MACHINE,
                          (subkey_ + L"\\" + path).c_str()) == ERROR_SUCCESS;
}

} // namespace wid::core
