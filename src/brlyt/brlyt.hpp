#pragma once

#ifndef RESERVOIR_BRLYT_H
#define RESERVOIR_BRLYT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Reservoir::Brlyt {

struct Chunk {
    std::string          tag;
    std::vector<uint8_t> body;
};

struct Document {
    std::string        magic   = "RLYT";
    uint16_t           version = 0;
    std::vector<Chunk> chunks;
};

constexpr size_t kChunkHeaderSize = 8;
constexpr size_t kPaneNameOffset  = 0xC;
constexpr size_t kPaneNameLength  = 16;
constexpr size_t kPaneTranslateOffset = 0x24;
constexpr size_t kPaneSizeOffset      = 0x44;

Document             parse(const std::vector<uint8_t>& data);
std::vector<uint8_t> serialize(const Document& doc);

bool isPaneTag(const std::string& tag);

std::string paneName(const Chunk& chunk);
void        setPaneName(Chunk& chunk, const std::string& name);

float getPaneFloat(const Chunk& chunk, size_t offset);
void  setPaneFloat(Chunk& chunk, size_t offset, float value);

constexpr size_t kNoPane = static_cast<size_t>(-1);

size_t findPane(const Document& doc, const std::string& name);

std::pair<size_t, size_t> subtreeRange(const Document& doc, size_t paneIndex);

using RenameRules = std::vector<std::pair<std::string, std::string>>;

struct CloneResult {
    size_t                                          index = 0;
    std::vector<std::pair<std::string, std::string>> renamed;
};

CloneResult cloneSubtree(Document& doc, const std::string& sourceName, const RenameRules& rules);

} // namespace Reservoir::Brlyt

#endif
