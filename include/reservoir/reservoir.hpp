#pragma once
#ifndef RESERVOIR_H
#define RESERVOIR_H

#include <filesystem>
#include <string>
#include <vector>

namespace Reservoir {

struct PatchResult {
    std::string          new_filename;
    std::string          arc;
    std::string          action;
    std::string          blo_name;
    std::vector<uint8_t> data;
};

std::vector<PatchResult> process_layout(
    const std::filesystem::path& layout_folder,
    const std::filesystem::path& patch_folder,
    const std::filesystem::path& output_folder);

} // namespace Reservoir

#endif