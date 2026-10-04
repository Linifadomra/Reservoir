#include "brlyt/brlyt.hpp"

#include <cstring>
#include <stdexcept>

namespace Reservoir::Brlyt {

namespace {

constexpr size_t   kFileHeaderMinSize = 0x10;
constexpr uint16_t kBigEndianMark     = 0xFEFF;

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

uint32_t readU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

void writeU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void writeU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void requirePane(const Chunk& chunk) {
    if (!isPaneTag(chunk.tag) || chunk.body.size() < kPaneSizeOffset - kChunkHeaderSize + 8) {
        throw std::runtime_error("brlyt: chunk '" + chunk.tag + "' is not a pane");
    }
}

} // namespace

Document parse(const std::vector<uint8_t>& data) {
    if (data.size() < kFileHeaderMinSize || (std::memcmp(data.data(), "RLYT", 4) != 0 && std::memcmp(data.data(), "RLAN", 4) != 0)) {
        throw std::runtime_error("brlyt: missing RLYT or RLAN signature");
    }
    if (readU16(&data[4]) != kBigEndianMark) {
        throw std::runtime_error("brlyt: only big-endian files are supported");
    }

    Document doc;
    doc.magic.assign(reinterpret_cast<const char*>(data.data()), 4);
    doc.version = readU16(&data[6]);

    const uint32_t fileSize   = readU32(&data[8]);
    const uint16_t headerSize = readU16(&data[0xC]);
    const uint16_t blockCount = readU16(&data[0xE]);

    if (fileSize != data.size()) {
        throw std::runtime_error("brlyt: file size field does not match data length");
    }
    if (headerSize != kFileHeaderMinSize) {
        throw std::runtime_error("brlyt: unexpected header size");
    }

    size_t pos = headerSize;
    for (uint16_t i = 0; i < blockCount; i++) {
        if (pos + kChunkHeaderSize > data.size()) {
            throw std::runtime_error("brlyt: truncated chunk header");
        }

        const uint32_t size = readU32(&data[pos + 4]);
        if (size < kChunkHeaderSize || pos + size > data.size()) {
            throw std::runtime_error("brlyt: bad chunk size");
        }

        Chunk chunk;
        chunk.tag.assign(reinterpret_cast<const char*>(&data[pos]), 4);
        chunk.body.assign(data.begin() + pos + kChunkHeaderSize, data.begin() + pos + size);
        doc.chunks.push_back(std::move(chunk));
        pos += size;
    }

    if (pos != data.size()) {
        throw std::runtime_error("brlyt: trailing bytes after last chunk");
    }

    return doc;
}

std::vector<uint8_t> serialize(const Document& doc) {
    std::vector<uint8_t> out;
    out.insert(out.end(), doc.magic.begin(), doc.magic.end());
    writeU16(out, kBigEndianMark);
    writeU16(out, doc.version);
    writeU32(out, 0);
    writeU16(out, kFileHeaderMinSize);
    writeU16(out, static_cast<uint16_t>(doc.chunks.size()));

    for (const Chunk& chunk : doc.chunks) {
        out.insert(out.end(), chunk.tag.begin(), chunk.tag.end());
        writeU32(out, static_cast<uint32_t>(chunk.body.size() + kChunkHeaderSize));
        out.insert(out.end(), chunk.body.begin(), chunk.body.end());
    }

    const uint32_t total = static_cast<uint32_t>(out.size());
    out[8]  = static_cast<uint8_t>(total >> 24);
    out[9]  = static_cast<uint8_t>(total >> 16);
    out[10] = static_cast<uint8_t>(total >> 8);
    out[11] = static_cast<uint8_t>(total);
    return out;
}

bool isPaneTag(const std::string& tag) {
    return tag == "pan1" || tag == "pic1" || tag == "txt1" || tag == "wnd1" || tag == "bnd1";
}

std::string paneName(const Chunk& chunk) {
    requirePane(chunk);
    const char* name = reinterpret_cast<const char*>(&chunk.body[kPaneNameOffset - kChunkHeaderSize]);
    return std::string(name, strnlen(name, kPaneNameLength));
}

void setPaneName(Chunk& chunk, const std::string& name) {
    requirePane(chunk);
    if (name.size() >= kPaneNameLength) {
        throw std::runtime_error("brlyt: pane name too long: " + name);
    }
    uint8_t* dst = &chunk.body[kPaneNameOffset - kChunkHeaderSize];
    std::memset(dst, 0, kPaneNameLength);
    std::memcpy(dst, name.data(), name.size());
}

float getPaneFloat(const Chunk& chunk, size_t offset) {
    requirePane(chunk);
    const uint32_t bits = readU32(&chunk.body[offset - kChunkHeaderSize]);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void setPaneFloat(Chunk& chunk, size_t offset, float value) {
    requirePane(chunk);
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    uint8_t* dst = &chunk.body[offset - kChunkHeaderSize];
    dst[0] = static_cast<uint8_t>(bits >> 24);
    dst[1] = static_cast<uint8_t>(bits >> 16);
    dst[2] = static_cast<uint8_t>(bits >> 8);
    dst[3] = static_cast<uint8_t>(bits);
}

size_t findPane(const Document& doc, const std::string& name) {
    for (size_t i = 0; i < doc.chunks.size(); i++) {
        if (isPaneTag(doc.chunks[i].tag) && paneName(doc.chunks[i]) == name) {
            return i;
        }
    }
    return kNoPane;
}

std::pair<size_t, size_t> subtreeRange(const Document& doc, size_t paneIndex) {
    if (paneIndex >= doc.chunks.size() || !isPaneTag(doc.chunks[paneIndex].tag)) {
        throw std::runtime_error("brlyt: index is not a pane");
    }

    size_t end = paneIndex + 1;
    if (end < doc.chunks.size() && doc.chunks[end].tag == "pas1") {
        int depth = 0;
        for (; end < doc.chunks.size(); end++) {
            if (doc.chunks[end].tag == "pas1") {
                depth++;
            } else if (doc.chunks[end].tag == "pae1") {
                depth--;
                if (depth == 0) {
                    end++;
                    break;
                }
            }
        }
        if (depth != 0) {
            throw std::runtime_error("brlyt: unbalanced pas1/pae1");
        }
    }

    return {paneIndex, end};
}

CloneResult cloneSubtree(Document& doc, const std::string& sourceName, const RenameRules& rules) {
    const size_t source = findPane(doc, sourceName);
    if (source == kNoPane) {
        throw std::runtime_error("brlyt: source pane not found: " + sourceName);
    }
    if (rules.empty()) {
        throw std::runtime_error("brlyt: clone needs at least one rename rule");
    }

    const auto [begin, end] = subtreeRange(doc, source);
    std::vector<Chunk> copy(doc.chunks.begin() + begin, doc.chunks.begin() + end);

    CloneResult result;
    std::vector<std::string> newNames;
    for (Chunk& chunk : copy) {
        if (!isPaneTag(chunk.tag)) {
            continue;
        }

        std::string name = paneName(chunk);
        bool renamed     = false;
        for (const auto& [from, to] : rules) {
            const size_t at = from.empty() ? std::string::npos : name.find(from);
            if (at == std::string::npos) {
                continue;
            }
            name.replace(at, from.size(), to);
            renamed = true;
            break;
        }
        if (!renamed) {
            throw std::runtime_error("brlyt: no rename rule matches pane " + name);
        }

        if (findPane(doc, name) != kNoPane) {
            throw std::runtime_error("brlyt: cloned pane name already exists: " + name);
        }
        for (const std::string& existing : newNames) {
            if (existing == name) {
                throw std::runtime_error("brlyt: rename rules produce duplicate pane name: " + name);
            }
        }
        newNames.push_back(name);
        result.renamed.emplace_back(paneName(chunk), name);
        setPaneName(chunk, name);
    }

    doc.chunks.insert(doc.chunks.begin() + end, copy.begin(), copy.end());
    result.index = end;
    return result;
}

} // namespace Reservoir::Brlyt
