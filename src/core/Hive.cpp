#include "core/Hive.h"

#include "util/Log.h"

#include <windows.h>

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

OfflineHive::OfflineHive(const fs::path& hiveFile, const std::wstring& subkey)
    : hiveFile_(hiveFile), subkey_(subkey) {
    enableHivePrivileges();

    auto& log = util::Log::instance();
    std::error_code ec;
    if (!fs::exists(hiveFile_, ec)) {
        log.error(L"Hive file does not exist: " + hiveFile_.wstring(), L"Hive");
        return;
    }

    // Make sure the hive file is writable; DISM occasionally leaves
    // FILE_ATTRIBUTE_READONLY on hive files inside a mounted WIM and
    // RegLoadKey then fails with ERROR_INVALID_PARAMETER (87).
    DWORD attrs = GetFileAttributesW(hiveFile_.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(hiveFile_.c_str(),
                           attrs & ~FILE_ATTRIBUTE_READONLY);
    }

    // Defensive: a previous crashed build may have left the subkey
    // registered. Try to unload any stale registration first.
    RegUnLoadKeyW(HKEY_LOCAL_MACHINE, subkey_.c_str());

    LONG r = RegLoadKeyW(HKEY_LOCAL_MACHINE, subkey_.c_str(),
                         hiveFile_.c_str());
    if (r != ERROR_SUCCESS) {
        auto sz = fs::file_size(hiveFile_, ec);
        log.error(L"RegLoadKey failed for " + hiveFile_.wstring() +
                  L" (error " + std::to_wstring(r) +
                  L", size=" + std::to_wstring(sz) +
                  L", attrs=" + std::to_wstring(attrs) + L")", L"Hive");
        return;
    }
    loaded_ = true;
}

OfflineHive::~OfflineHive() {
    if (loaded_) {
        LONG r = RegUnLoadKeyW(HKEY_LOCAL_MACHINE, subkey_.c_str());
        if (r != ERROR_SUCCESS) {
            util::Log::instance().warn(
                L"RegUnLoadKey failed for " + subkey_ +
                L" (error " + std::to_wstring(r) + L")", L"Hive");
        }
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
