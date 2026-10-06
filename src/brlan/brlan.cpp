#include "brlan/brlan.hpp"
#include "common/endian.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Reservoir::Brlan {

namespace {

constexpr size_t kBodyBase            = Brlyt::kChunkHeaderSize;
constexpr size_t kPaiCountOffset      = 0xE;
constexpr size_t kPaiTableOffsetField = 0x10;
constexpr size_t kContentNameLength   = 20;

struct ContentSpan {
    std::string name;
    size_t      begin;
    size_t      end;
};

std::string contentName(const std::vector<uint8_t>& chunk, size_t at) {
    const char* name = reinterpret_cast<const char*>(&chunk[at]);
    return std::string(name, strnlen(name, kContentNameLength));
}

} // namespace

size_t cloneTracks(Brlyt::Document& doc, const NamePairs& names) {
    if (doc.magic != "RLAN") {
        throw std::runtime_error("brlan: document is not an RLAN file");
    }

    Brlyt::Chunk* pai = nullptr;
    for (Brlyt::Chunk& chunk : doc.chunks) {
        if (chunk.tag == "pai1") {
            pai = &chunk;
            break;
        }
    }
    if (!pai) {
        throw std::runtime_error("brlan: no pai1 chunk");
    }

    const bool le = pai->littleEndian;

    std::vector<uint8_t> chunk(Brlyt::kChunkHeaderSize, 0);
    chunk.insert(chunk.end(), pai->body.begin(), pai->body.end());
    if (chunk.size() < kPaiTableOffsetField + 4) {
        throw std::runtime_error("brlan: pai1 too small");
    }

    const uint16_t count       = Endian::readU16(&chunk[kPaiCountOffset], le);
    const uint32_t tableOffset = Endian::readU32(&chunk[kPaiTableOffsetField], le);
    if (tableOffset + static_cast<size_t>(count) * 4 > chunk.size()) {
        throw std::runtime_error("brlan: content table out of range");
    }

    std::vector<uint32_t> offsets(count);
    for (uint16_t i = 0; i < count; i++) {
        offsets[i] = Endian::readU32(&chunk[tableOffset + i * 4], le);
    }

    std::vector<ContentSpan> spans;
    for (uint16_t i = 0; i < count; i++) {
        const size_t end = (i + 1 < count) ? offsets[i + 1] : chunk.size();
        if (offsets[i] + kContentNameLength > chunk.size() || end < offsets[i] || end > chunk.size()) {
            throw std::runtime_error("brlan: content block out of range");
        }
        spans.push_back({contentName(chunk, offsets[i]), offsets[i], end});
    }

    struct Planned {
        size_t      source;
        std::string name;
    };
    std::vector<Planned> planned;

    for (const auto& [from, to] : names) {
        if (to.size() >= kContentNameLength) {
            throw std::runtime_error("brlan: track name too long: " + to);
        }
        const auto source = std::find_if(spans.begin(), spans.end(), [&](const ContentSpan& s) { return s.name == from; });
        if (source == spans.end()) {
            continue;
        }
        const bool taken = std::any_of(spans.begin(), spans.end(), [&](const ContentSpan& s) { return s.name == to; }) ||
                           std::any_of(planned.begin(), planned.end(), [&](const Planned& p) { return p.name == to; });
        if (taken) {
            throw std::runtime_error("brlan: track already exists: " + to);
        }
        planned.push_back({static_cast<size_t>(source - spans.begin()), to});
    }

    if (planned.empty()) {
        return 0;
    }

    const size_t   added      = planned.size();
    const uint32_t tableShift = static_cast<uint32_t>(added * 4);
    const size_t   tableEnd   = tableOffset + static_cast<size_t>(count) * 4;

    std::vector<uint8_t> rebuilt(chunk.begin(), chunk.begin() + tableEnd);
    rebuilt.resize(tableEnd + tableShift, 0);
    rebuilt.insert(rebuilt.end(), chunk.begin() + tableEnd, chunk.end());

    for (uint16_t i = 0; i < count; i++) {
        Endian::writeU32(&rebuilt[tableOffset + i * 4], offsets[i] + tableShift, le);
    }

    for (size_t n = 0; n < added; n++) {
        const ContentSpan& source = spans[planned[n].source];
        const size_t       at     = rebuilt.size();
        rebuilt.insert(rebuilt.end(), chunk.begin() + source.begin, chunk.begin() + source.end);
        std::memset(&rebuilt[at], 0, kContentNameLength);
        std::memcpy(&rebuilt[at], planned[n].name.data(), planned[n].name.size());
        Endian::writeU32(&rebuilt[tableOffset + (count + n) * 4], static_cast<uint32_t>(at), le);
    }

    Endian::writeU16(&rebuilt[kPaiCountOffset], static_cast<uint16_t>(count + added), le);

    pai->body.assign(rebuilt.begin() + Brlyt::kChunkHeaderSize, rebuilt.end());
    return added;
}

} // namespace Reservoir::Brlan
