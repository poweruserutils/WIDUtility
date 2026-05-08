#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace wid::core {

namespace fs = std::filesystem;

using ProgressFn = std::function<void(std::wstring_view stage, int percent)>;

struct IsoExtractOptions {
    fs::path sourceIso;        // input .iso
    fs::path destDir;          // empty dir to populate
};

struct IsoBuildOptions {
    fs::path sourceDir;        // populated tree (matches what was extracted)
    fs::path destIso;          // output path
    std::wstring volumeLabel;  // e.g. "CCCOMA_X64FRE_EN-US_DV9"
    bool uefiBootable = true;
    bool biosBootable = true;
};

bool extractIso(const IsoExtractOptions& opts, const ProgressFn& progress);
bool buildIso  (const IsoBuildOptions&   opts, const ProgressFn& progress);

} // namespace wid::core
