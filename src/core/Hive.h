#pragma once

#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace wid::core {

namespace fs = std::filesystem;

// RAII wrapper around RegLoadKey / RegUnLoadKey on an offline hive file.
// `hiveFile` must point to a registry hive (e.g. <mount>\Windows\System32\config\SOFTWARE).
// `subkey` is a transient name under HKLM (e.g. "WID_OFFLINE_SOFTWARE").
class OfflineHive {
public:
    OfflineHive(const fs::path& hiveFile, const std::wstring& subkey);
    ~OfflineHive();

    OfflineHive(const OfflineHive&) = delete;
    OfflineHive& operator=(const OfflineHive&) = delete;

    bool ok() const { return loaded_; }
    HKEY root() const { return root_; }                 // HKLM
    const std::wstring& mountSubkey() const { return subkey_; }

    // Convenience setters relative to the loaded hive root.
    bool setDword (const std::wstring& path, const std::wstring& name, DWORD value);
    bool setString(const std::wstring& path, const std::wstring& name, const std::wstring& value);
    bool deleteValue(const std::wstring& path, const std::wstring& name);
    bool deleteTree(const std::wstring& path);

private:
    fs::path     hiveFile_;       // original path inside the WIM mount
    fs::path     stagedFile_;     // local copy actually loaded (empty if direct)
    fs::path     stageDir_;       // owning dir of stagedFile_ (cleaned on dtor)
    std::wstring subkey_;          // empty when loaded via RegLoadAppKey
    HKEY         root_   = HKEY_LOCAL_MACHINE;
    bool         loaded_ = false;
    bool         appKeyMode_ = false;  // true => root_ is a RegLoadAppKey handle

    // Acquire SeRestorePrivilege + SeBackupPrivilege; required for RegLoadKey.
    static bool enableHivePrivileges();
};

} // namespace wid::core
