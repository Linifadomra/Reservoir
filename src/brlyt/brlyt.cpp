#include "brlyt/brlyt.hpp"

#include <cstring>
#include <stdexcept>

namespace Reservoir::Brlyt {

namespace {

constexpr size_t kFileHeaderMinSize = 0x10;

std::string reversed(std::string value) {
    return std::string(value.rbegin(), value.rend());
}

std::string readTag(const uint8_t* p, bool littleEndian) {
    std::string tag(reinterpret_cast<const char*>(p), 4);
    return littleEndian ? reversed(tag) : tag;
}

void appendU16(std::vector<uint8_t>& out, uint16_t v, bool littleEndian) {
    out.resize(out.size() + 2);
    writeU16(&out[out.size() - 2], v, littleEndian);
}

void appendU32(std::vector<uint8_t>& out, uint32_t v, bool littleEndian) {
    out.resize(out.size() + 4);
    writeU32(&out[out.size() - 4], v, littleEndian);
}

void requirePane(const Chunk& chunk) {
    if (!isPaneTag(chunk.tag) || chunk.body.size() < kPaneSizeOffset - kChunkHeaderSize + 8) {
        throw std::runtime_error("brlyt: chunk '" + chunk.tag + "' is not a pane");
    }
}

} // namespace

uint16_t readU16(const uint8_t* p, bool le) {
    return le ? static_cast<uint16_t>((p[1] << 8) | p[0]) : static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t readU32(const uint8_t* p, bool le) {
    if (le) {
        return (static_cast<uint32_t>(p[3]) << 24) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0];
    }
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

void writeU16(uint8_t* p, uint16_t v, bool le) {
    p[le ? 1 : 0] = static_cast<uint8_t>(v >> 8);
    p[le ? 0 : 1] = static_cast<uint8_t>(v);
}

void writeU32(uint8_t* p, uint32_t v, bool le) {
    for (int i = 0; i < 4; i++) {
        p[le ? i : 3 - i] = static_cast<uint8_t>(v >> (8 * i));
    }
}

Document parse(const std::vector<uint8_t>& data) {
    if (data.size() < kFileHeaderMinSize) {
        throw std::runtime_error("brlyt: file too small");
    }

    Document doc;
    const std::string signature(reinterpret_cast<const char*>(data.data()), 4);
    for (const char* known : {"RLYT", "RLAN"}) {
        if (signature == known) {
            doc.magic = known;
        } else if (signature == reversed(known)) {
            doc.magic        = known;
            doc.littleEndian = true;
        }
    }
    if (signature != doc.magic && signature != reversed(doc.magic)) {
        throw std::runtime_error("brlyt: missing RLYT or RLAN signature");
    }

    const bool le = doc.littleEndian;
    if (readU16(&data[4], le) != 0xFEFF) {
        throw std::runtime_error("brlyt: byte order mark does not match signature");
    }

    doc.version = readU16(&data[6], le);

    const uint32_t fileSize   = readU32(&data[8], le);
    const uint16_t headerSize = readU16(&data[0xC], le);
    const uint16_t blockCount = readU16(&data[0xE], le);

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

        const uint32_t size = readU32(&data[pos + 4], le);
        if (size < kChunkHeaderSize || pos + size > data.size()) {
            throw std::runtime_error("brlyt: bad chunk size");
        }

        Chunk chunk;
        chunk.tag          = readTag(&data[pos], le);
        chunk.littleEndian = le;
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
    const bool le = doc.littleEndian;

    std::vector<uint8_t> out;
    const std::string magic = le ? reversed(doc.magic) : doc.magic;
    out.insert(out.end(), magic.begin(), magic.end());
    appendU16(out, 0xFEFF, le);
    appendU16(out, doc.version, le);
    appendU32(out, 0, le);
    appendU16(out, kFileHeaderMinSize, le);
    appendU16(out, static_cast<uint16_t>(doc.chunks.size()), le);

    for (const Chunk& chunk : doc.chunks) {
        const std::string tag = le ? reversed(chunk.tag) : chunk.tag;
        out.insert(out.end(), tag.begin(), tag.end());
        appendU32(out, static_cast<uint32_t>(chunk.body.size() + kChunkHeaderSize), le);
        out.insert(out.end(), chunk.body.begin(), chunk.body.end());
    }

    writeU32(&out[8], static_cast<uint32_t>(out.size()), le);
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
    const uint32_t bits = readU32(&chunk.body[offset - kChunkHeaderSize], chunk.littleEndian);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void setPaneFloat(Chunk& chunk, size_t offset, float value) {
    requirePane(chunk);
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(&chunk.body[offset - kChunkHeaderSize], bits, chunk.littleEndian);
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

CloneResult cloneSubtree(Document& doc, const std::string& sourceName, const RenameRules& rules, const std::string& parentName) {
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

    size_t insertAt = end;
    if (!parentName.empty()) {
        const size_t parent = findPane(doc, parentName);
        if (parent == kNoPane) {
            throw std::runtime_error("brlyt: parent pane not found: " + parentName);
        }

        const auto [parentBegin, parentEnd] = subtreeRange(doc, parent);
        if (parent >= begin && parent < end) {
            throw std::runtime_error("brlyt: parent pane lies inside the cloned subtree: " + parentName);
        }

        if (parentEnd == parentBegin + 1) {
            Chunk open;
            open.tag          = "pas1";
            open.littleEndian = doc.littleEndian;
            Chunk close;
            close.tag          = "pae1";
            close.littleEndian = doc.littleEndian;
            copy.insert(copy.begin(), open);
            copy.push_back(close);
            insertAt = parentEnd;
        } else {
            insertAt = parentEnd - 1;
        }
    }

    doc.chunks.insert(doc.chunks.begin() + insertAt, copy.begin(), copy.end());
    result.index = insertAt;
    return result;
}

} // namespace Reservoir::Brlyt
