#pragma once

#ifndef RESERVOIR_ENDIAN_H
#define RESERVOIR_ENDIAN_H

#include <cstdint>
#include <cstring>

namespace Reservoir::Endian {

inline uint16_t readU16(const uint8_t* p, bool le) {
    return le ? static_cast<uint16_t>((p[1] << 8) | p[0]) : static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline uint32_t readU32(const uint8_t* p, bool le) {
    if (le) {
        return (static_cast<uint32_t>(p[3]) << 24) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0];
    }
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

inline float readF32(const uint8_t* p, bool le) {
    const uint32_t bits = readU32(p, le);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline void writeU16(uint8_t* p, uint16_t v, bool le) {
    p[le ? 1 : 0] = static_cast<uint8_t>(v >> 8);
    p[le ? 0 : 1] = static_cast<uint8_t>(v);
}

inline void writeU32(uint8_t* p, uint32_t v, bool le) {
    for (int i = 0; i < 4; i++) {
        p[le ? i : 3 - i] = static_cast<uint8_t>(v >> (8 * i));
    }
}

inline void writeF32(uint8_t* p, float v, bool le) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    writeU32(p, bits, le);
}

} // namespace Reservoir::Endian

#endif
