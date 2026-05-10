#pragma once

#ifndef RESERVOIR_MAT1_H
#define RESERVOIR_MAT1_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Reservoir {

struct MAT1SectionOffsets;
struct MatInitDataSection;

struct GXColor {
    uint8_t r, g, b, a;
};

struct GXColorS10 {
    uint16_t r, g, b, a;   
};

struct J2DTextureSRTInfo {
    float scale_x;
    float scale_y;
    float rotation_deg;
    float translate_x;
    float translate_y;
};

struct J2DColorChanInfo {
    uint8_t field_0, field_1, field_2, field_3;
};

struct J2DTexCoordInfo {
    uint8_t tex_gen_type;
    uint8_t tex_gen_src;
    uint8_t tex_gen_mtx;
    uint8_t padding;
};

struct J2DTexMtxInfo {
    uint8_t type;
    uint8_t dcc;
    uint8_t field_2;
    uint8_t field_3;
    float center_x;
    float center_y;
    float center_z;
    J2DTextureSRTInfo srt;
};

struct J2DTevOrderInfo {
    uint8_t tex_coord;
    uint8_t tex_map;
    uint8_t color;
    uint8_t field_3;
};

struct J2DTevStageInfo {
    uint8_t field_0;
    uint8_t color_a, color_b, color_c, color_d;
    uint8_t c_op, c_bias, c_scale, c_clamp, c_reg;
    uint8_t alpha_a, alpha_b, alpha_c, alpha_d;
    uint8_t a_op, a_bias, a_scale, a_clamp, a_reg;
    uint8_t field_13;
};

struct J2DTevSwapModeInfo {
    uint8_t ras_sel, tex_sel, field_2, field_3;
};

struct J2DTevSwapModeTblInfo {
    uint8_t field_0, field_1, field_2, field_3;
};

struct J2DAlphaCompInfo {
    uint8_t field_0, field_1;
    uint8_t ref_0, ref_1;
    uint8_t field_4, field_5, field_6, field_7;
};

struct J2DBlendInfo {
    uint8_t type;
    uint8_t src_factor;
    uint8_t dst_factor;
    uint8_t op;
};

struct J2DMaterialInitData {
    uint8_t  mat_mode;
    uint8_t  cull_mode_idx;
    uint8_t  color_chan_num_idx;
    uint8_t  tex_gen_num_idx;
    uint8_t  tev_stage_num_idx;
    uint8_t  dither_idx;
    uint8_t  mat_alpha_calc;
    uint8_t  unknown_7;

    
    std::array<uint16_t, 2>  mat_color_idx;
    
    std::array<uint16_t, 4>  color_chan_info_idx;
    
    std::array<uint16_t, 8>  tex_coord_info_idx;
    std::array<uint16_t, 10> tex_mtx_info_idx;
    std::array<uint16_t, 8>  tex_no_idx;
    uint16_t font_no_idx;
    
    std::array<uint16_t, 4>  tev_k_color_idx;
    
    std::array<uint8_t,  16> tev_k_color_sel;
    std::array<uint8_t,  16> tev_k_alpha_sel;
    
    std::array<uint16_t, 16> tev_order_info_idx;
    std::array<uint16_t, 4>  tev_color_idx;
    std::array<uint16_t, 16> tev_stage_info_idx;
    std::array<uint16_t, 16> tev_swap_mode_info_idx;
    std::array<uint16_t, 4>  tev_swap_mode_tbl_idx;
    
    uint16_t alpha_comp_info_idx;
    uint16_t blend_info_idx;
    uint16_t unknown_E6;
};

struct MAT1SubsectionCounts {
    uint32_t cull_mode          = 0;
    uint32_t mat_color          = 0;
    uint32_t color_chan_num     = 0;
    uint32_t color_chan_info    = 0;
    uint32_t tex_gen_num        = 0;
    uint32_t tex_coord_info     = 0;
    uint32_t tex_mtx_info       = 0;
    uint32_t tex_no             = 0;
    uint32_t font_no            = 0;
    uint32_t tev_order_info     = 0;
    uint32_t tev_color          = 0;
    uint32_t tev_k_color        = 0;
    uint32_t tev_stage_num      = 0;
    uint32_t tev_stage_info     = 0;
    uint32_t tev_swap_mode_info = 0;
    uint32_t tev_swap_mode_tbl  = 0;
    uint32_t alpha_comp_info    = 0;
    uint32_t blend_info         = 0;
    uint32_t dither             = 0;
};



struct MatInitDataSection {
    std::vector<J2DMaterialInitData> entries;
    std::vector<uint8_t>             padding;
    MAT1SubsectionCounts             counts;   
};

struct MatInitIdxSection {
    std::vector<uint16_t>  indexes;
    std::vector<uint8_t>   padding;
};

struct MatNameTableHeaderEntry {
    uint16_t id;
    uint16_t name_offset;
};

struct MatNameTableSection {
    uint16_t num_entries;
    uint16_t start_padding;
    std::vector<MatNameTableHeaderEntry> header_entries;
    
    std::vector<std::string>             mat_names;
    std::vector<uint8_t>                 padding;
};

struct IndInitDataSection {
    std::vector<uint8_t> data;
};

struct CullModeSection {
    std::vector<uint32_t> cull_modes;
    std::vector<uint8_t>  padding;
};

struct MatColorSection {
    std::vector<GXColor> colors;
    std::vector<uint8_t> padding;
};

struct ColorChanNumSection {
    std::vector<uint8_t> values;
    std::vector<uint8_t> padding;
};

struct ColorChanInfoSection {
    std::vector<J2DColorChanInfo> entries;
    std::vector<uint8_t>          padding;
};

struct TexGenNumSection {
    std::vector<uint8_t> values;
    std::vector<uint8_t> padding;
};

struct TexCoordInfoSection {
    std::vector<J2DTexCoordInfo> entries;
    std::vector<uint8_t>         padding;
};

struct TexMtxInfoSection {
    std::vector<J2DTexMtxInfo> entries;
    std::vector<uint8_t>       padding;
};

struct TexNoSection {
    std::vector<uint16_t> tex_no;
    std::vector<uint8_t>  padding;
};

struct FontNoSection {
    std::vector<uint16_t> font_no;
    std::vector<uint8_t>  padding;
};

struct TevOrderInfoSection {
    std::vector<J2DTevOrderInfo> entries;
    std::vector<uint8_t>         padding;
};

struct TevColorSection {
    std::vector<GXColorS10> colors;
    std::vector<uint8_t>    padding;
};

struct TevKColorSection {
    std::vector<GXColor> colors;
    std::vector<uint8_t> padding;
};

struct TevStageNumSection {
    std::vector<uint8_t> values;
    std::vector<uint8_t> padding;
};

struct TevStageInfoSection {
    std::vector<J2DTevStageInfo> entries;
    std::vector<uint8_t>         padding;
};

struct TevSwapModeInfoSection {
    std::vector<J2DTevSwapModeInfo> entries;
    std::vector<uint8_t>            padding;
};

struct TevSwapModeTblSection {
    std::vector<J2DTevSwapModeTblInfo> entries;
    std::vector<uint8_t>               padding;
};

struct AlphaCompInfoSection {
    std::vector<J2DAlphaCompInfo> entries;
    std::vector<uint8_t>          padding;
};

struct BlendInfoSection {
    std::vector<J2DBlendInfo> entries;
    std::vector<uint8_t>      padding;
};

struct DitherSection {
    std::vector<uint8_t> dither;
    std::vector<uint8_t> padding;
};



struct MAT1SectionOffsets {
    uint32_t mat_init_data         = 0;
    uint32_t mat_init_data_indexes = 0;
    uint32_t mat_name_table        = 0;
    uint32_t ind_init_data         = 0;
    uint32_t cull_mode             = 0;
    uint32_t mat_color             = 0;
    uint32_t color_chan_num        = 0;
    uint32_t color_chan_info       = 0;
    uint32_t tex_gen_num           = 0;
    uint32_t tex_coord_info        = 0;
    uint32_t tex_mtx_info          = 0;
    uint32_t tex_no                = 0;
    uint32_t font_no               = 0;
    uint32_t tev_order_info        = 0;
    uint32_t tev_color             = 0;
    uint32_t tev_k_color           = 0;
    uint32_t tev_stage_num         = 0;
    uint32_t tev_stage_info        = 0;
    uint32_t tev_swap_mode_info    = 0;
    uint32_t tev_swap_mode_tbl     = 0;
    uint32_t alpha_comp_info       = 0;
    uint32_t blend_info            = 0;
    uint32_t dither                = 0;
};



struct MAT1Section {
    uint32_t section_size;
    uint16_t material_count;
    uint16_t header_padding;

    MAT1SectionOffsets  offsets;
    MatInitDataSection  mat_init;
    MatInitIdxSection   mat_init_idx;
    MatNameTableSection mat_name_table;
    IndInitDataSection  ind_init;
    CullModeSection     cull_modes;
    MatColorSection     mat_colors;
    ColorChanNumSection color_chan_num;
    ColorChanInfoSection color_chan_info;
    TexGenNumSection    tex_gen_num;
    TexCoordInfoSection tex_coord_info;
    TexMtxInfoSection   tex_mtx_info;
    TexNoSection        tex_no;
    FontNoSection       font_no;
    TevOrderInfoSection tev_order_info;
    TevColorSection     tev_color;
    TevKColorSection    tev_k_color;
    TevStageNumSection  tev_stage_num;
    TevStageInfoSection tev_stage_info;
    TevSwapModeInfoSection  tev_swap_mode_info;
    TevSwapModeTblSection   tev_swap_mode_tbl;
    AlphaCompInfoSection    alpha_comp_info;
    BlendInfoSection        blend_info;
    DitherSection           dither;

    std::vector<uint8_t> end_padding;
};

void parse_mat1_offsets (const uint8_t* base, size_t* pos, MAT1SectionOffsets&);
void serialize_mat1_offsets(std::vector<uint8_t>& out, const MAT1SectionOffsets&);

void parse_mat_init_data_section    (const uint8_t* base, const MAT1SectionOffsets&, MatInitDataSection&);
void serialize_mat_init_data_section(std::vector<uint8_t>& out, const MatInitDataSection&);

void parse_mat_init_idx_section    (const uint8_t* base, const MAT1SectionOffsets&, MatInitIdxSection&);
void serialize_mat_init_idx_section(std::vector<uint8_t>& out, const MatInitIdxSection&);

void parse_mat_name_table_section    (const uint8_t* base, const MAT1SectionOffsets&, MatNameTableSection&);
void serialize_mat_name_table_section(std::vector<uint8_t>& out, const MatNameTableSection&);

void parse_ind_init_data_section    (const uint8_t* base, const MAT1SectionOffsets&, IndInitDataSection&);
void serialize_ind_init_data_section(std::vector<uint8_t>& out, const IndInitDataSection&);

void parse_cull_mode_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, CullModeSection&);
void serialize_cull_mode_section(std::vector<uint8_t>& out, const CullModeSection&);

void parse_mat_color_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, MatColorSection&);
void serialize_mat_color_section(std::vector<uint8_t>& out, const MatColorSection&);

void parse_color_chan_num_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, ColorChanNumSection&);
void serialize_color_chan_num_section(std::vector<uint8_t>& out, const ColorChanNumSection&);

void parse_color_chan_info_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, ColorChanInfoSection&);
void serialize_color_chan_info_section(std::vector<uint8_t>& out, const ColorChanInfoSection&);

void parse_tex_gen_num_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, TexGenNumSection&);
void serialize_tex_gen_num_section(std::vector<uint8_t>& out, const TexGenNumSection&);

void parse_tex_coord_info_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, TexCoordInfoSection&);
void serialize_tex_coord_info_section(std::vector<uint8_t>& out, const TexCoordInfoSection&);

void parse_tex_mtx_info_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, TexMtxInfoSection&);
void serialize_tex_mtx_info_section(std::vector<uint8_t>& out, const TexMtxInfoSection&);

void parse_tex_no_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, TexNoSection&);
void serialize_tex_no_section(std::vector<uint8_t>& out, const TexNoSection&);

void parse_font_no_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, FontNoSection&);
void serialize_font_no_section(std::vector<uint8_t>& out, const FontNoSection&);

void parse_tev_order_info_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, TevOrderInfoSection&);
void serialize_tev_order_info_section(std::vector<uint8_t>& out, const TevOrderInfoSection&);

void parse_tev_color_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, TevColorSection&);
void serialize_tev_color_section(std::vector<uint8_t>& out, const TevColorSection&);

void parse_tev_k_color_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, TevKColorSection&);
void serialize_tev_k_color_section(std::vector<uint8_t>& out, const TevKColorSection&);

void parse_tev_stage_num_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, TevStageNumSection&);
void serialize_tev_stage_num_section(std::vector<uint8_t>& out, const TevStageNumSection&);

void parse_tev_stage_info_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, TevStageInfoSection&);
void serialize_tev_stage_info_section(std::vector<uint8_t>& out, const TevStageInfoSection&);

void parse_tev_swap_mode_info_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, TevSwapModeInfoSection&);
void serialize_tev_swap_mode_info_section(std::vector<uint8_t>& out, const TevSwapModeInfoSection&);

void parse_tev_swap_mode_tbl_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, TevSwapModeTblSection&);
void serialize_tev_swap_mode_tbl_section(std::vector<uint8_t>& out, const TevSwapModeTblSection&);

void parse_alpha_comp_info_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, AlphaCompInfoSection&);
void serialize_alpha_comp_info_section(std::vector<uint8_t>& out, const AlphaCompInfoSection&);

void parse_blend_info_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, BlendInfoSection&);
void serialize_blend_info_section(std::vector<uint8_t>& out, const BlendInfoSection&);

void parse_dither_section    (const uint8_t* base, const MAT1SectionOffsets&, const MatInitDataSection&, uint32_t mat1_section_size, DitherSection&);
void serialize_dither_section(std::vector<uint8_t>& out, const DitherSection&);

void parse_mat1_section    (const uint8_t* buf, size_t* pos_inout, MAT1Section&);
void serialize_mat1_section(std::vector<uint8_t>& out, const MAT1Section&);

}

#endif