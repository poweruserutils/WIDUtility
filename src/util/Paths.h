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

} // namespace wid::util
