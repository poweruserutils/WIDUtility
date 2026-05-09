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
    if (!a || !b) {
        util::Log::instance().warn(
            std::wstring(L"Hive privilege grant: SeRestore=") +
            (a ? L"ok" : L"FAIL") + L" SeBackup=" + (b ? L"ok" : L"FAIL"),
            L"Hive");
    }
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

    // Stage the log companions alongside. Modern Win11 hives need .LOG1
    // (and sometimes .LOG2) present at load time even when the primary's
    // sequence numbers match — the loader uses them as part of the
    // incremental-log format. Without them RegLoadAppKey returns BADDB.
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

    // Diagnostic: log the first 8 bytes of the staged hive so we can verify
    // it starts with the expected "regf" signature (0x66676572).
    {
        HANDLE fh = CreateFileW(stagedFile_.c_str(), GENERIC_READ,
                                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fh != INVALID_HANDLE_VALUE) {
            BYTE hdr[32] = {};
            DWORD got = 0;
            ReadFile(fh, hdr, sizeof(hdr), &got, nullptr);
            CloseHandle(fh);
            DWORD sig    = *reinterpret_cast<DWORD*>(&hdr[0]);
            DWORD seq1   = *reinterpret_cast<DWORD*>(&hdr[4]);
            DWORD seq2   = *reinterpret_cast<DWORD*>(&hdr[8]);
            DWORD vMajor = *reinterpret_cast<DWORD*>(&hdr[20]);
            DWORD vMinor = *reinterpret_cast<DWORD*>(&hdr[24]);
            wchar_t buf[256];
            swprintf_s(buf,
                L"hive sig=0x%08X seq1=%u seq2=%u dirty=%s ver=%u.%u",
                sig, seq1, seq2, (seq1 == seq2 ? L"no" : L"YES"),
                vMajor, vMinor);
            log.info(buf, L"Hive");
        }
    }

    // Diagnostic: follow the hbin chain by size, not by page. Each hbin
    // header is 32 bytes; offset 0..3 is "hbin", offset 8..11 is the
    // hbin's total size (multiple of 4096, can span many 4 KB pages).
    // We start at file offset 0x1000 and advance by each hbin's size
    // until EOF. Any failure to land on a valid signature means the
    // chain is broken — i.e. WimFsf isn't materializing the full file.
    {
        HANDLE fh = CreateFileW(stagedFile_.c_str(), GENERIC_READ,
                                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fh != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER fsz{};
            GetFileSizeEx(fh, &fsz);
            const DWORD kHbinSig = 0x6E696268; // "hbin" LE
            uint64_t pos = 0x1000;
            uint64_t hbinCount = 0;
            uint64_t totalBytes = 0;
            uint64_t breakAt = 0;
            bool ok = true;
            while ((int64_t)pos + 32 <= fsz.QuadPart) {
                LARGE_INTEGER off{}; off.QuadPart = pos;
                SetFilePointerEx(fh, off, nullptr, FILE_BEGIN);
                BYTE hdr[32]{};
                DWORD got = 0;
                if (!ReadFile(fh, hdr, sizeof(hdr), &got, nullptr) ||
                    got != sizeof(hdr)) { ok = false; breakAt = pos; break; }
                DWORD sig  = *reinterpret_cast<DWORD*>(&hdr[0]);
                DWORD size = *reinterpret_cast<DWORD*>(&hdr[8]);
                if (sig != kHbinSig || size < 0x1000 || (size % 0x1000) != 0) {
                    ok = false; breakAt = pos; break;
                }
                hbinCount++;
                totalBytes += size;
                pos += size;
            }
            CloseHandle(fh);
            wchar_t buf[256];
            if (ok) {
                swprintf_s(buf,
                    L"hbin walk OK: %llu hbins, %llu bytes, ends at 0x%llX (file size 0x%llX)",
                    (unsigned long long)hbinCount,
                    (unsigned long long)totalBytes,
                    (unsigned long long)pos,
                    (unsigned long long)fsz.QuadPart);
                log.info(buf, L"Hive");
            } else {
                swprintf_s(buf,
                    L"hbin walk BROKEN at file offset 0x%llX after %llu hbins (file size 0x%llX)",
                    (unsigned long long)breakAt,
                    (unsigned long long)hbinCount,
                    (unsigned long long)fsz.QuadPart);
                log.error(buf, L"Hive");
            }
        }
    }

    // Defensive: a previous crashed build may have left the subkey registered.
    RegUnLoadKeyW(HKEY_LOCAL_MACHINE, subkey_.c_str());

    LONG r = RegLoadKeyW(HKEY_LOCAL_MACHINE, subkey_.c_str(),
                         stagedFile_.c_str());
    if (r == ERROR_SUCCESS) {
        loaded_ = true;
        return;
    }

    auto sz = fs::file_size(stagedFile_, ec);
    DWORD attrs = GetFileAttributesW(stagedFile_.c_str());
    log.warn(L"RegLoadKey failed for staged " + stagedFile_.wstring() +
             L" (error " + std::to_wstring(r) +
             L", size=" + std::to_wstring(sz) +
             L", attrs=" + std::to_wstring(attrs) +
             L"); falling back to RegLoadAppKey", L"Hive");

    // Fallback: RegLoadAppKey accepts arbitrary hive files, doesn't require
    // a subkey under HKLM, and is more tolerant of modern hive formats than
    // RegLoadKey. The returned handle is the hive root itself; we use it
    // directly via root_ and clear subkey_ so setters address keys relative
    // to the hive root.
    HKEY appRoot = nullptr;
    LONG ar = RegLoadAppKeyW(stagedFile_.c_str(), &appRoot,
                             KEY_ALL_ACCESS, 0, 0);
    if (ar != ERROR_SUCCESS) {
        log.warn(L"RegLoadAppKey also failed for " + stagedFile_.wstring() +
                 L" (error " + std::to_wstring(ar) +
                 L"); trying reg.exe load shell-out", L"Hive");

        // Final fallback: shell out to reg.exe. Same underlying API but the
        // OS tool sometimes succeeds where direct calls don't (different
        // process token, different open share modes).
        std::wstring cmd = L"reg.exe load \"HKLM\\" + subkey_ + L"\" \"" +
                           stagedFile_.wstring() + L"\"";
        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::wstring mut = cmd;
        DWORD exitCode = (DWORD)-1;
        if (CreateProcessW(nullptr, mut.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 30000);
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        if (exitCode == 0) {
            log.info(L"reg.exe load succeeded; using HKLM\\" + subkey_,
                     L"Hive");
            // root_ stays HKLM; subkey_ stays as-is; appKeyMode_ stays false.
            loaded_ = true;
            return;
        }
        log.error(L"reg.exe load also failed (exit=" +
                  std::to_wstring(exitCode) + L")", L"Hive");
        return;
    }
    root_       = appRoot;
    subkey_.clear();
    appKeyMode_ = true;
    loaded_     = true;
}

OfflineHive::~OfflineHive() {
    auto& log = util::Log::instance();
    if (loaded_) {
        if (appKeyMode_) {
            // RegCloseKey on the RegLoadAppKey handle unloads the hive and
            // flushes pending writes to the underlying file.
            RegCloseKey(root_);
        } else {
            LONG r = RegUnLoadKeyW(HKEY_LOCAL_MACHINE, subkey_.c_str());
            if (r != ERROR_SUCCESS) {
                log.warn(L"RegUnLoadKey failed for " + subkey_ +
                         L" (error " + std::to_wstring(r) + L")", L"Hive");
            }
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

// Build the subkey path for either load mode:
//  - RegLoadKey mode: HKLM\<subkey>\<path>      → "<subkey>\\<path>"
//  - RegLoadAppKey mode: addressed off root_    → "<path>"
static std::wstring joinPath(const std::wstring& base, const std::wstring& path) {
    if (base.empty()) return path;
    return base + L"\\" + path;
}

bool OfflineHive::setDword(const std::wstring& path, const std::wstring& name, DWORD value) {
    if (!loaded_) return false;
    HKEY h = openOrCreate(root_, joinPath(subkey_, path));
    if (!h) return false;
    LONG r = RegSetValueExW(h, name.c_str(), 0, REG_DWORD,
                            reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool OfflineHive::setString(const std::wstring& path, const std::wstring& name,
                            const std::wstring& value) {
    if (!loaded_) return false;
    HKEY h = openOrCreate(root_, joinPath(subkey_, path));
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
    if (RegOpenKeyExW(root_, joinPath(subkey_, path).c_str(),
                      0, KEY_ALL_ACCESS, &h) != ERROR_SUCCESS) return false;
    LONG r = RegDeleteValueW(h, name.c_str());
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool OfflineHive::deleteTree(const std::wstring& path) {
    if (!loaded_) return false;
    return RegDeleteTreeW(root_, joinPath(subkey_, path).c_str()) == ERROR_SUCCESS;
}

} // namespace wid::core
