#pragma once

#include <cstdint>
#include <vector>

struct DecodedImage {
    int                  width  = 0;
    int                  height = 0;
    bool                 intensity = false;
    std::vector<uint8_t> rgba;
};

bool decode_gx_texture(const std::vector<uint8_t>& file, DecodedImage& out);
