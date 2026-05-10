#pragma once

#ifndef RESERVOIR_BLO_H
#define RESERVOIR_BLO_H

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "mat1/mat1.hpp"

namespace Reservoir {

constexpr uint8_t PADDING_BYTES[] = {
    'T','h','i','s',' ','i','s',' ','p','a','d','d','i','n','g',
    ' ','d','a','t','a',' ','t','o',' ','a','l','i','g','n',' '
};
static constexpr size_t PADDING_LENGTH = sizeof(PADDING_BYTES);

struct INF1Section {
    uint32_t size;
    uint16_t width;
    uint16_t height;
    uint8_t  values[4];
    std::vector<uint8_t> padding;
};

struct TEX1Reference {
    uint8_t     res_type;
    std::string texture_name;
};

struct TEX1Section {
    uint32_t section_size;
    uint16_t header_size;
    uint16_t texture_count;
    std::vector<TEX1Reference> references;
    std::vector<uint8_t> padding;
};

struct FNT1Reference {
    uint8_t     res_type;
    std::string font_name;
};

struct FNT1Section {
    uint32_t section_size;
    uint16_t header_size;
    std::vector<FNT1Reference> references;
    std::vector<uint8_t> padding;
};

struct ElementNode;

struct PAN2Node {
    uint16_t field_0x8;
    uint16_t bck_idx;
    uint8_t  visible;
    uint8_t  base_position;
    uint8_t  padding[2];
    std::string info_tag;
    std::string user_info_tag;
    float    size_x, size_y;
    float    scale_x, scale_y;
    float    rotate_x, rotate_y, rotate_z;
    float    translate_x, translate_y;
    uint8_t  end_padding[4];

    std::vector<struct ElementNode> children;
};

struct PIC2Node {
    PAN2Node base;
    uint32_t pan2_sub_tag_size = 0;
    uint16_t field_0x0;
    uint16_t field_0x2;
    uint16_t material_num;
    uint16_t field_0x6;
    uint16_t field_0x8[4];
    uint16_t field_0x10[8];
    uint32_t corner_color[4];
    std::vector<uint8_t> end_padding;
};

struct TBX2Node {
    PAN2Node base;
    uint32_t pan2_sub_tag_size = 0;
    uint16_t field_0x0;
    uint16_t field_0x2;
    uint16_t material_num;
    uint16_t char_space;
    uint16_t line_space;
    uint16_t font_size_x;
    uint16_t font_size_y;
    uint8_t  h_bind;
    uint8_t  v_bind;
    uint8_t  char_color[4];
    uint8_t  grad_color[4];
    uint8_t  connected;
    uint8_t  field_0x19[3];
    uint16_t field_0x1c;
    uint16_t field_0x1e;
    std::vector<uint8_t> end_padding; // text + alignment padding
};

struct WIN2Node {
    PAN2Node             base;
    uint32_t pan2_sub_tag_size = 0;
    std::vector<uint8_t> data;
};

struct ElementNode {
    enum class Type : uint8_t { PAN2, PIC2, TBX2, WIN2 };
    Type type;
    std::variant<PAN2Node, PIC2Node, TBX2Node, WIN2Node> node;

    bool     has_leading_bgn1_tag   = false;
    uint32_t leading_bgn1_tag_size  = 0;
    bool     has_children_bgn1_tag  = false;
    uint32_t children_bgn1_tag_size = 0;
    bool     has_end_tag    = false;
    uint32_t end_tag_size   = 0;
};

struct BLO {
    std::string  name;
    uint8_t      tag[4];
    uint8_t      type[4];
    uint32_t     size;
    uint32_t     blocks;
    uint8_t      header_padding[16];
    INF1Section  inf1;
    TEX1Section  tex1;
    FNT1Section  fnt1;
    MAT1Section  mat1;
    ElementNode  root;
    std::vector<uint8_t> padding;
};

BLO parse_blo(const std::vector<uint8_t>& data);
std::vector<uint8_t> serialize_blo(const BLO& blo);

} // namespace Reservoir

#endif