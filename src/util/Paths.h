#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace wid::util {

namespace fs = std::filesystem;

// Locate a tool by absolute path. Returns nullopt if not found.
std::optional<fs::path> findDism();
std::optional<fs::path> findOscdimg();
std::optional<fs::path> findReg();

// Per-run scratch root under %TEMP%\WIDUtility.
fs::path createScratchRoot();

fs::path isoExtractDir(const fs::path& scratchRoot);
fs::path wimMountDir  (const fs::path& scratchRoot);
fs::path stageDir     (const fs::path& scratchRoot);

// Desktop\WID Utility Logs\ — created if it does not exist.
fs::path desktopLogsDir();

// Per-session log file path under desktopLogsDir(). Cached after first call,
// so all sinks see the same filename.
fs::path currentLogFile();

// Remove every previous scratch root under %TEMP%\WIDUtility\. Safe to
// call at startup: nothing in there is in use yet.
void cleanOldScratchRoots();

} // namespace wid::util
