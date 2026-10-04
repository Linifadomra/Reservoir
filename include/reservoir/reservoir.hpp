#pragma once
#ifndef RESERVOIR_H
#define RESERVOIR_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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
    const std::filesystem::path& output_folder,
    const std::vector<std::string>& patch_jsons);

class LayoutPatches {
public:
    LayoutPatches();
    ~LayoutPatches();

    LayoutPatches(const LayoutPatches&)            = delete;
    LayoutPatches& operator=(const LayoutPatches&) = delete;

    void add(const std::string& json_text);

    bool empty() const;

    std::optional<std::vector<uint8_t>> patch_layout(
        const std::string& archive,
        const std::string& file,
        const uint8_t*     data,
        size_t             size) const;

    std::optional<std::vector<uint8_t>> patch_animation(
        const std::string& archive,
        const uint8_t*     data,
        size_t             size) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Reservoir

#endif