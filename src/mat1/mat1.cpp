#include "blo/blo.hpp"
#include "mat1.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <confluence/endian.h>
#include <vector>

namespace Reservoir {

static constexpr uint32_t MAT1_HEADER_SIZE   = 12u;
static constexpr uint32_t OFFSET_TABLE_BYTES = 23u * 4u;
static constexpr uint32_t INIT_DATA_STRIDE   = 232u;

static uint8_t  read_u8   (const uint8_t* buf, size_t* pos) { uint8_t  v = buf[*pos]; *pos += 1; return v; }
static uint16_t read_be16 (const uint8_t* buf, size_t* pos) { uint16_t v = gc_be16(buf + *pos); *pos += 2; return v; }
static uint32_t read_be32 (const uint8_t* buf, size_t* pos) { uint32_t v = gc_be32(buf + *pos); *pos += 4; return v; }
static float    read_bef32(const uint8_t* buf, size_t* pos) { float    v = gc_be_f32(buf + *pos); *pos += 4; return v; }

static void write_u8  (std::vector<uint8_t>& out, uint8_t  v){ out.push_back(v); }
static void write_be16(std::vector<uint8_t>& out, uint16_t v){ out.resize(out.size()+2); gc_write_be16(out.data()+out.size()-2, v); }
static void write_be32(std::vector<uint8_t>& out, uint32_t v){ out.resize(out.size()+4); gc_write_be32(out.data()+out.size()-4, v); }

static void write_raw(std::vector<uint8_t>& out, const uint8_t* src, size_t n)
{
    out.insert(out.end(), src, src + n);
}

void parse_mat1_offsets(const uint8_t* buf, size_t* pos, MAT1SectionOffsets& o)
{
    o.mat_init_data         = read_be32(buf, pos);
    o.mat_init_data_indexes = read_be32(buf, pos);
    o.mat_name_table        = read_be32(buf, pos);
    o.ind_init_data         = read_be32(buf, pos);
    o.cull_mode             = read_be32(buf, pos);
    o.mat_color             = read_be32(buf, pos);
    o.color_chan_num        = read_be32(buf, pos);
    o.color_chan_info       = read_be32(buf, pos);
    o.tex_gen_num           = read_be32(buf, pos);
    o.tex_coord_info        = read_be32(buf, pos);
    o.tex_mtx_info          = read_be32(buf, pos);
    o.tex_no                = read_be32(buf, pos);
    o.font_no               = read_be32(buf, pos);
    o.tev_order_info        = read_be32(buf, pos);
    o.tev_color             = read_be32(buf, pos);
    o.tev_k_color           = read_be32(buf, pos);
    o.tev_stage_num         = read_be32(buf, pos);
    o.tev_stage_info        = read_be32(buf, pos);
    o.tev_swap_mode_info    = read_be32(buf, pos);
    o.tev_swap_mode_tbl     = read_be32(buf, pos);
    o.alpha_comp_info       = read_be32(buf, pos);
    o.blend_info            = read_be32(buf, pos);
    o.dither                = read_be32(buf, pos);
}

void serialize_mat1_offsets(std::vector<uint8_t>& out, const MAT1SectionOffsets& o)
{
    write_be32(out, o.mat_init_data);
    write_be32(out, o.mat_init_data_indexes);
    write_be32(out, o.mat_name_table);
    write_be32(out, o.ind_init_data);
    write_be32(out, o.cull_mode);
    write_be32(out, o.mat_color);
    write_be32(out, o.color_chan_num);
    write_be32(out, o.color_chan_info);
    write_be32(out, o.tex_gen_num);
    write_be32(out, o.tex_coord_info);
    write_be32(out, o.tex_mtx_info);
    write_be32(out, o.tex_no);
    write_be32(out, o.font_no);
    write_be32(out, o.tev_order_info);
    write_be32(out, o.tev_color);
    write_be32(out, o.tev_k_color);
    write_be32(out, o.tev_stage_num);
    write_be32(out, o.tev_stage_info);
    write_be32(out, o.tev_swap_mode_info);
    write_be32(out, o.tev_swap_mode_tbl);
    write_be32(out, o.alpha_comp_info);
    write_be32(out, o.blend_info);
    write_be32(out, o.dither);
}



static uint32_t next_valid_offset(const MAT1SectionOffsets& o, uint32_t current)
{
    const uint32_t all[] = {
        o.mat_init_data, o.mat_init_data_indexes, o.mat_name_table,
        o.ind_init_data, o.cull_mode, o.mat_color, o.color_chan_num,
        o.color_chan_info, o.tex_gen_num, o.tex_coord_info, o.tex_mtx_info,
        o.tex_no, o.font_no, o.tev_order_info, o.tev_color, o.tev_k_color,
        o.tev_stage_num, o.tev_stage_info, o.tev_swap_mode_info,
        o.tev_swap_mode_tbl, o.alpha_comp_info, o.blend_info, o.dither
    };
    uint32_t best = 0;
    for (uint32_t v : all)
        if (v != 0 && v > current && (best == 0 || v < best))
            best = v;
    return best;
}



static void parse_init_data_entry(const uint8_t* buf, size_t* pos,
                                   J2DMaterialInitData& d)
{
    d.mat_mode           = read_u8(buf, pos);
    d.cull_mode_idx      = read_u8(buf, pos);
    d.color_chan_num_idx = read_u8(buf, pos);
    d.tex_gen_num_idx    = read_u8(buf, pos);
    d.tev_stage_num_idx  = read_u8(buf, pos);
    d.dither_idx         = read_u8(buf, pos);
    d.mat_alpha_calc     = read_u8(buf, pos);
    d.unknown_7          = read_u8(buf, pos);

    for (auto& v : d.mat_color_idx)           v = read_be16(buf, pos);
    for (auto& v : d.color_chan_info_idx)      v = read_be16(buf, pos);
    for (auto& v : d.tex_coord_info_idx)       v = read_be16(buf, pos);
    for (auto& v : d.tex_mtx_info_idx)         v = read_be16(buf, pos);
    for (auto& v : d.tex_no_idx)               v = read_be16(buf, pos);
    d.font_no_idx = read_be16(buf, pos);
    for (auto& v : d.tev_k_color_idx)          v = read_be16(buf, pos);
    for (auto& v : d.tev_k_color_sel)          v = read_u8(buf, pos);
    for (auto& v : d.tev_k_alpha_sel)          v = read_u8(buf, pos);
    for (auto& v : d.tev_order_info_idx)       v = read_be16(buf, pos);
    for (auto& v : d.tev_color_idx)            v = read_be16(buf, pos);
    for (auto& v : d.tev_stage_info_idx)       v = read_be16(buf, pos);
    for (auto& v : d.tev_swap_mode_info_idx)   v = read_be16(buf, pos);
    for (auto& v : d.tev_swap_mode_tbl_idx)    v = read_be16(buf, pos);
    d.alpha_comp_info_idx = read_be16(buf, pos);
    d.blend_info_idx      = read_be16(buf, pos);
    d.unknown_E6          = read_be16(buf, pos);
}

static void serialize_init_data_entry(std::vector<uint8_t>& out,
                                       const J2DMaterialInitData& d)
{
    write_u8(out, d.mat_mode);
    write_u8(out, d.cull_mode_idx);
    write_u8(out, d.color_chan_num_idx);
    write_u8(out, d.tex_gen_num_idx);
    write_u8(out, d.tev_stage_num_idx);
    write_u8(out, d.dither_idx);
    write_u8(out, d.mat_alpha_calc);
    write_u8(out, d.unknown_7);

    for (auto v : d.mat_color_idx)             write_be16(out, v);
    for (auto v : d.color_chan_info_idx)       write_be16(out, v);
    for (auto v : d.tex_coord_info_idx)        write_be16(out, v);
    for (auto v : d.tex_mtx_info_idx)          write_be16(out, v);
    for (auto v : d.tex_no_idx)                write_be16(out, v);
    write_be16(out, d.font_no_idx);
    for (auto v : d.tev_k_color_idx)           write_be16(out, v);
    for (auto v : d.tev_k_color_sel)           write_u8(out, v);
    for (auto v : d.tev_k_alpha_sel)           write_u8(out, v);
    for (auto v : d.tev_order_info_idx)        write_be16(out, v);
    for (auto v : d.tev_color_idx)             write_be16(out, v);
    for (auto v : d.tev_stage_info_idx)        write_be16(out, v);
    for (auto v : d.tev_swap_mode_info_idx)    write_be16(out, v);
    for (auto v : d.tev_swap_mode_tbl_idx)     write_be16(out, v);
    write_be16(out, d.alpha_comp_info_idx);
    write_be16(out, d.blend_info_idx);
    write_be16(out, d.unknown_E6);
}

static void compute_section_counts(MatInitDataSection& s)
{

    auto& c = s.counts;
    std::memset(&c, 0, sizeof(c));

    for (const auto& d : s.entries) {
        auto upd = [](uint32_t& acc, uint16_t v){ if(v != 0xFFFF && v > acc) acc = v; };

        upd(c.cull_mode,              d.cull_mode_idx);
        upd(c.color_chan_num,         d.color_chan_num_idx);
        upd(c.tex_gen_num,            d.tex_gen_num_idx);
        upd(c.tev_stage_num,          d.tev_stage_num_idx);
        upd(c.dither,                 d.dither_idx);
        upd(c.alpha_comp_info,        d.alpha_comp_info_idx);
        upd(c.blend_info,             d.blend_info_idx);
        upd(c.font_no,                d.font_no_idx);

        for (auto v : d.mat_color_idx)            upd(c.mat_color,           v);
        for (auto v : d.color_chan_info_idx)      upd(c.color_chan_info,     v);
        for (auto v : d.tex_coord_info_idx)       upd(c.tex_coord_info,      v);
        for (auto v : d.tex_mtx_info_idx)         upd(c.tex_mtx_info,        v);
        for (auto v : d.tex_no_idx)               upd(c.tex_no,              v);
        for (auto v : d.tev_k_color_idx)          upd(c.tev_k_color,         v);
        for (auto v : d.tev_order_info_idx)       upd(c.tev_order_info,      v);
        for (auto v : d.tev_color_idx)            upd(c.tev_color,           v);
        for (auto v : d.tev_stage_info_idx)       upd(c.tev_stage_info,      v);
        for (auto v : d.tev_swap_mode_info_idx)   upd(c.tev_swap_mode_info,  v);
        for (auto v : d.tev_swap_mode_tbl_idx)    upd(c.tev_swap_mode_tbl,   v);
    }


    c.cull_mode++;  c.mat_color++;       c.color_chan_num++;
    c.color_chan_info++;  c.tex_gen_num++;  c.tex_coord_info++;
    c.tex_mtx_info++;  c.tex_no++;  c.font_no++;
    c.tev_order_info++;  c.tev_color++;  c.tev_k_color++;
    c.tev_stage_num++;   c.tev_stage_info++;  c.tev_swap_mode_info++;
    c.tev_swap_mode_tbl++;  c.alpha_comp_info++;  c.blend_info++;
    c.dither++;
}

void parse_mat_init_data_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                  MatInitDataSection& out)
{


    uint32_t span = offsets.mat_init_data_indexes - offsets.mat_init_data;
    uint32_t count = span / INIT_DATA_STRIDE;
    uint32_t padding_bytes_count = span - count * INIT_DATA_STRIDE;

    size_t pos = offsets.mat_init_data;
    out.entries.resize(count);
    for (auto& e : out.entries)
        parse_init_data_entry(base, &pos, e);

    out.padding.assign(base + pos, base + pos + padding_bytes_count);
    compute_section_counts(out);
}

void serialize_mat_init_data_section(std::vector<uint8_t>& out,
                                      const MatInitDataSection& s)
{
    for (const auto& e : s.entries)
        serialize_init_data_entry(out, e);
    write_raw(out, s.padding.data(), s.padding.size());
}



void parse_mat_init_idx_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                 MatInitIdxSection& out)
{
    uint32_t count = (offsets.mat_init_data_indexes - offsets.mat_init_data) / INIT_DATA_STRIDE;
    size_t pos = offsets.mat_init_data_indexes;
    out.indexes.resize(count);
    for (auto& v : out.indexes) v = read_be16(base, &pos);


    uint32_t raw_size = count * 2u;
    uint32_t next = offsets.mat_name_table;
    out.padding.assign(base + pos, base + next);
}

void serialize_mat_init_idx_section(std::vector<uint8_t>& out,
                                     const MatInitIdxSection& s)
{
    for (auto v : s.indexes) write_be16(out, v);
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_mat_name_table_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                   MatNameTableSection& out)
{
    size_t pos = offsets.mat_name_table;
    out.num_entries   = read_be16(base, &pos);
    out.start_padding = read_be16(base, &pos);

    out.header_entries.resize(out.num_entries);
    for (auto& e : out.header_entries) {
        e.id          = read_be16(base, &pos);
        e.name_offset = read_be16(base, &pos);
    }

    out.mat_names.resize(out.num_entries);
    for (uint32_t i = 0; i < out.num_entries; ++i) {
        if (i + 1 < out.num_entries) {
            out.mat_names[i].assign(
                base + offsets.mat_name_table + out.header_entries[i].name_offset,
                base + offsets.mat_name_table + out.header_entries[i + 1].name_offset);
        } else {
            size_t start = offsets.mat_name_table + out.header_entries[i].name_offset;
            size_t end = start;
            while (base[end] != '\0') ++end;
            ++end;
            out.mat_names[i].assign(base + start, base + end);
        }
    }

    size_t names_end = offsets.mat_name_table
                       + out.header_entries[out.num_entries - 1].name_offset
                       + out.mat_names.back().size();

    uint32_t nxt = next_valid_offset(offsets, offsets.mat_name_table);
    if (nxt == 0) nxt = (uint32_t)names_end;
    out.padding.assign(base + names_end, base + nxt);
}

void serialize_mat_name_table_section(std::vector<uint8_t>& out,
                                       const MatNameTableSection& s)
{
    write_be16(out, s.num_entries);
    write_be16(out, s.start_padding);
    for (const auto& e : s.header_entries) {
        write_be16(out, e.id);
        write_be16(out, e.name_offset);
    }
    for (const auto& name : s.mat_names)
        write_raw(out, (const uint8_t*)name.data(), name.size());
    write_raw(out, s.padding.data(), s.padding.size());
}



void parse_ind_init_data_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                  IndInitDataSection& out)
{
    if (offsets.ind_init_data == 0) { out.data.clear(); return; }
    uint32_t len = offsets.cull_mode - offsets.ind_init_data;
    out.data.assign(base + offsets.ind_init_data, base + offsets.ind_init_data + len);
}

void serialize_ind_init_data_section(std::vector<uint8_t>& out,
                                      const IndInitDataSection& s)
{
    write_raw(out, s.data.data(), s.data.size());
}



void parse_cull_mode_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                              const MatInitDataSection& init, CullModeSection& out)
{
    if (offsets.cull_mode == 0) return;
    size_t pos = offsets.cull_mode;
    out.cull_modes.resize(init.counts.cull_mode);
    for (auto& v : out.cull_modes) v = read_be32(base, &pos);

    uint32_t nxt = next_valid_offset(offsets, offsets.cull_mode);
    uint32_t raw = init.counts.cull_mode * 4u;
    uint32_t span = nxt - offsets.cull_mode;
    out.padding.assign(base + pos, base + offsets.cull_mode + span);
}

void serialize_cull_mode_section(std::vector<uint8_t>& out, const CullModeSection& s)
{
    for (auto v : s.cull_modes) write_be32(out, v);
    write_raw(out, s.padding.data(), s.padding.size());
}



void parse_mat_color_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                              const MatInitDataSection& init, MatColorSection& out)
{
    if (offsets.mat_color == 0) return;
    size_t pos = offsets.mat_color;
    out.colors.resize(init.counts.mat_color);
    for (auto& c : out.colors) {
        c.r = read_u8(base, &pos);
        c.g = read_u8(base, &pos);
        c.b = read_u8(base, &pos);
        c.a = read_u8(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.mat_color);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_mat_color_section(std::vector<uint8_t>& out, const MatColorSection& s)
{
    for (const auto& c : s.colors) {
        write_u8(out, c.r); write_u8(out, c.g);
        write_u8(out, c.b); write_u8(out, c.a);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}



void parse_color_chan_num_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                   const MatInitDataSection& init, ColorChanNumSection& out)
{
    if (offsets.color_chan_num == 0) return;
    size_t pos = offsets.color_chan_num;
    out.values.resize(init.counts.color_chan_num);
    for (auto& v : out.values) v = read_u8(base, &pos);
    uint32_t nxt = next_valid_offset(offsets, offsets.color_chan_num);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_color_chan_num_section(std::vector<uint8_t>& out,
                                       const ColorChanNumSection& s)
{
    for (auto v : s.values) write_u8(out, v);
    write_raw(out, s.padding.data(), s.padding.size());
}



void parse_color_chan_info_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                    const MatInitDataSection& init, ColorChanInfoSection& out)
{
    if (offsets.color_chan_info == 0) return;
    size_t pos = offsets.color_chan_info;
    out.entries.resize(init.counts.color_chan_info);
    for (auto& e : out.entries) {
        e.field_0 = read_u8(base, &pos);
        e.field_1 = read_u8(base, &pos);
        e.field_2 = read_u8(base, &pos);
        e.field_3 = read_u8(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.color_chan_info);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_color_chan_info_section(std::vector<uint8_t>& out,
                                        const ColorChanInfoSection& s)
{
    for (const auto& e : s.entries) {
        write_u8(out, e.field_0); write_u8(out, e.field_1);
        write_u8(out, e.field_2); write_u8(out, e.field_3);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}



void parse_tex_gen_num_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                const MatInitDataSection& init, TexGenNumSection& out)
{
    if (offsets.tex_gen_num == 0) return;
    size_t pos = offsets.tex_gen_num;
    out.values.resize(init.counts.tex_gen_num);
    for (auto& v : out.values) v = read_u8(base, &pos);
    uint32_t nxt = next_valid_offset(offsets, offsets.tex_gen_num);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_tex_gen_num_section(std::vector<uint8_t>& out,
                                    const TexGenNumSection& s)
{
    for (auto v : s.values) write_u8(out, v);
    write_raw(out, s.padding.data(), s.padding.size());
}



void parse_tex_coord_info_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                   const MatInitDataSection& init, TexCoordInfoSection& out)
{
    if (offsets.tex_coord_info == 0) return;
    size_t pos = offsets.tex_coord_info;
    out.entries.resize(init.counts.tex_coord_info);
    for (auto& e : out.entries) {
        e.tex_gen_type = read_u8(base, &pos);
        e.tex_gen_src  = read_u8(base, &pos);
        e.tex_gen_mtx  = read_u8(base, &pos);
        e.padding      = read_u8(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.tex_coord_info);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_tex_coord_info_section(std::vector<uint8_t>& out,
                                       const TexCoordInfoSection& s)
{
    for (const auto& e : s.entries) {
        write_u8(out, e.tex_gen_type); write_u8(out, e.tex_gen_src);
        write_u8(out, e.tex_gen_mtx);  write_u8(out, e.padding);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_tex_mtx_info_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                  const MatInitDataSection& init, TexMtxInfoSection& out)
{
    if (offsets.tex_mtx_info == 0) return;
    size_t pos = offsets.tex_mtx_info;
    out.entries.resize(init.counts.tex_mtx_info);
    for (auto& e : out.entries) {
        e.type        = read_u8(base, &pos);
        e.dcc         = read_u8(base, &pos);
        e.field_2     = read_u8(base, &pos);
        e.field_3     = read_u8(base, &pos);
        e.center_x    = read_bef32(base, &pos);
        e.center_y    = read_bef32(base, &pos);
        e.center_z    = read_bef32(base, &pos);
        e.srt.scale_x       = read_bef32(base, &pos);
        e.srt.scale_y       = read_bef32(base, &pos);
        e.srt.rotation_deg  = read_bef32(base, &pos);
        e.srt.translate_x   = read_bef32(base, &pos);
        e.srt.translate_y   = read_bef32(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.tex_mtx_info);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_tex_mtx_info_section(std::vector<uint8_t>& out,
                                     const TexMtxInfoSection& s)
{
    auto wf = [&](float f){
        out.resize(out.size()+4);
        gc_write_be_f32(out.data()+out.size()-4, f);
    };
    for (const auto& e : s.entries) {
        write_u8(out, e.type); write_u8(out, e.dcc);
        write_u8(out, e.field_2); write_u8(out, e.field_3);
        wf(e.center_x); wf(e.center_y); wf(e.center_z);
        wf(e.srt.scale_x); wf(e.srt.scale_y);
        wf(e.srt.rotation_deg);
        wf(e.srt.translate_x); wf(e.srt.translate_y);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_tex_no_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                           const MatInitDataSection& init, TexNoSection& out)
{
    if (offsets.tex_no == 0) return;
    size_t pos = offsets.tex_no;
    out.tex_no.resize(init.counts.tex_no);
    for (auto& v : out.tex_no) v = read_be16(base, &pos);
    uint32_t nxt = next_valid_offset(offsets, offsets.tex_no);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_tex_no_section(std::vector<uint8_t>& out, const TexNoSection& s)
{
    for (auto v : s.tex_no) write_be16(out, v);
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_font_no_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                            const MatInitDataSection& init, FontNoSection& out)
{
    if (offsets.font_no == 0) return;
    size_t pos = offsets.font_no;
    out.font_no.resize(init.counts.font_no);
    for (auto& v : out.font_no) v = read_be16(base, &pos);
    uint32_t nxt = next_valid_offset(offsets, offsets.font_no);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_font_no_section(std::vector<uint8_t>& out, const FontNoSection& s)
{
    for (auto v : s.font_no) write_be16(out, v);
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_tev_order_info_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                   const MatInitDataSection& init, TevOrderInfoSection& out)
{
    if (offsets.tev_order_info == 0) return;
    size_t pos = offsets.tev_order_info;
    out.entries.resize(init.counts.tev_order_info);
    for (auto& e : out.entries) {
        e.tex_coord = read_u8(base, &pos);
        e.tex_map   = read_u8(base, &pos);
        e.color     = read_u8(base, &pos);
        e.field_3   = read_u8(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.tev_order_info);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_tev_order_info_section(std::vector<uint8_t>& out,
                                       const TevOrderInfoSection& s)
{
    for (const auto& e : s.entries) {
        write_u8(out, e.tex_coord); write_u8(out, e.tex_map);
        write_u8(out, e.color);     write_u8(out, e.field_3);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_tev_color_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                              const MatInitDataSection& init, TevColorSection& out)
{
    if (offsets.tev_color == 0) return;
    size_t pos = offsets.tev_color;
    out.colors.resize(init.counts.tev_color);
    for (auto& c : out.colors) {
        c.r = read_be16(base, &pos);
        c.g = read_be16(base, &pos);
        c.b = read_be16(base, &pos);
        c.a = read_be16(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.tev_color);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_tev_color_section(std::vector<uint8_t>& out, const TevColorSection& s)
{
    for (const auto& c : s.colors) {
        write_be16(out, c.r); write_be16(out, c.g);
        write_be16(out, c.b); write_be16(out, c.a);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_tev_k_color_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                const MatInitDataSection& init, TevKColorSection& out)
{
    if (offsets.tev_k_color == 0) return;
    size_t pos = offsets.tev_k_color;
    out.colors.resize(init.counts.tev_k_color);
    for (auto& c : out.colors) {
        c.r = read_u8(base, &pos); c.g = read_u8(base, &pos);
        c.b = read_u8(base, &pos); c.a = read_u8(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.tev_k_color);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_tev_k_color_section(std::vector<uint8_t>& out, const TevKColorSection& s)
{
    for (const auto& c : s.colors) {
        write_u8(out, c.r); write_u8(out, c.g);
        write_u8(out, c.b); write_u8(out, c.a);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_tev_stage_num_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                  const MatInitDataSection& init, TevStageNumSection& out)
{
    if (offsets.tev_stage_num == 0) return;
    size_t pos = offsets.tev_stage_num;
    out.values.resize(init.counts.tev_stage_num);
    for (auto& v : out.values) v = read_u8(base, &pos);
    uint32_t nxt = next_valid_offset(offsets, offsets.tev_stage_num);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_tev_stage_num_section(std::vector<uint8_t>& out,
                                      const TevStageNumSection& s)
{
    for (auto v : s.values) write_u8(out, v);
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_tev_stage_info_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                                   const MatInitDataSection& init, TevStageInfoSection& out)
{
    if (offsets.tev_stage_info == 0) return;
    size_t pos = offsets.tev_stage_info;
    out.entries.resize(init.counts.tev_stage_info);
    for (auto& e : out.entries) {
        e.field_0  = read_u8(base, &pos);
        e.color_a  = read_u8(base, &pos); e.color_b = read_u8(base, &pos);
        e.color_c  = read_u8(base, &pos); e.color_d = read_u8(base, &pos);
        e.c_op     = read_u8(base, &pos); e.c_bias  = read_u8(base, &pos);
        e.c_scale  = read_u8(base, &pos); e.c_clamp = read_u8(base, &pos);
        e.c_reg    = read_u8(base, &pos);
        e.alpha_a  = read_u8(base, &pos); e.alpha_b = read_u8(base, &pos);
        e.alpha_c  = read_u8(base, &pos); e.alpha_d = read_u8(base, &pos);
        e.a_op     = read_u8(base, &pos); e.a_bias  = read_u8(base, &pos);
        e.a_scale  = read_u8(base, &pos); e.a_clamp = read_u8(base, &pos);
        e.a_reg    = read_u8(base, &pos);
        e.field_13 = read_u8(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.tev_stage_info);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_tev_stage_info_section(std::vector<uint8_t>& out,
                                       const TevStageInfoSection& s)
{
    for (const auto& e : s.entries) {
        write_u8(out, e.field_0);
        write_u8(out, e.color_a); write_u8(out, e.color_b);
        write_u8(out, e.color_c); write_u8(out, e.color_d);
        write_u8(out, e.c_op);    write_u8(out, e.c_bias);
        write_u8(out, e.c_scale); write_u8(out, e.c_clamp);
        write_u8(out, e.c_reg);
        write_u8(out, e.alpha_a); write_u8(out, e.alpha_b);
        write_u8(out, e.alpha_c); write_u8(out, e.alpha_d);
        write_u8(out, e.a_op);    write_u8(out, e.a_bias);
        write_u8(out, e.a_scale); write_u8(out, e.a_clamp);
        write_u8(out, e.a_reg);
        write_u8(out, e.field_13);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}



void parse_tev_swap_mode_info_section(const uint8_t* base,
                                       const MAT1SectionOffsets& offsets,
                                       const MatInitDataSection& init,
                                       TevSwapModeInfoSection& out)
{
    if (offsets.tev_swap_mode_info == 0) return;
    size_t pos = offsets.tev_swap_mode_info;
    out.entries.resize(init.counts.tev_swap_mode_info);
    for (auto& e : out.entries) {
        e.ras_sel = read_u8(base, &pos); e.tex_sel = read_u8(base, &pos);
        e.field_2 = read_u8(base, &pos); e.field_3 = read_u8(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.tev_swap_mode_info);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_tev_swap_mode_info_section(std::vector<uint8_t>& out,
                                           const TevSwapModeInfoSection& s)
{
    for (const auto& e : s.entries) {
        write_u8(out, e.ras_sel); write_u8(out, e.tex_sel);
        write_u8(out, e.field_2); write_u8(out, e.field_3);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_tev_swap_mode_tbl_section(const uint8_t* base,
                                      const MAT1SectionOffsets& offsets,
                                      const MatInitDataSection& init,
                                      TevSwapModeTblSection& out)
{
    if (offsets.tev_swap_mode_tbl == 0) return;
    size_t pos = offsets.tev_swap_mode_tbl;
    out.entries.resize(init.counts.tev_swap_mode_tbl);
    for (auto& e : out.entries) {
        e.field_0 = read_u8(base, &pos); e.field_1 = read_u8(base, &pos);
        e.field_2 = read_u8(base, &pos); e.field_3 = read_u8(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.tev_swap_mode_tbl);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_tev_swap_mode_tbl_section(std::vector<uint8_t>& out,
                                          const TevSwapModeTblSection& s)
{
    for (const auto& e : s.entries) {
        write_u8(out, e.field_0); write_u8(out, e.field_1);
        write_u8(out, e.field_2); write_u8(out, e.field_3);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_alpha_comp_info_section(const uint8_t* base,
                                    const MAT1SectionOffsets& offsets,
                                    const MatInitDataSection& init,
                                    AlphaCompInfoSection& out)
{
    if (offsets.alpha_comp_info == 0) return;
    size_t pos = offsets.alpha_comp_info;
    out.entries.resize(init.counts.alpha_comp_info);
    for (auto& e : out.entries) {
        e.field_0 = read_u8(base, &pos); e.field_1 = read_u8(base, &pos);
        e.ref_0   = read_u8(base, &pos); e.ref_1   = read_u8(base, &pos);
        e.field_4 = read_u8(base, &pos); e.field_5 = read_u8(base, &pos);
        e.field_6 = read_u8(base, &pos); e.field_7 = read_u8(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.alpha_comp_info);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_alpha_comp_info_section(std::vector<uint8_t>& out,
                                        const AlphaCompInfoSection& s)
{
    for (const auto& e : s.entries) {
        write_u8(out, e.field_0); write_u8(out, e.field_1);
        write_u8(out, e.ref_0);   write_u8(out, e.ref_1);
        write_u8(out, e.field_4); write_u8(out, e.field_5);
        write_u8(out, e.field_6); write_u8(out, e.field_7);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_blend_info_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                               const MatInitDataSection& init, BlendInfoSection& out)
{
    if (offsets.blend_info == 0) return;
    size_t pos = offsets.blend_info;
    out.entries.resize(init.counts.blend_info);
    for (auto& e : out.entries) {
        e.type       = read_u8(base, &pos);
        e.src_factor = read_u8(base, &pos);
        e.dst_factor = read_u8(base, &pos);
        e.op         = read_u8(base, &pos);
    }
    uint32_t nxt = next_valid_offset(offsets, offsets.blend_info);
    out.padding.assign(base + pos, base + nxt);
}

void serialize_blend_info_section(std::vector<uint8_t>& out, const BlendInfoSection& s)
{
    for (const auto& e : s.entries) {
        write_u8(out, e.type); write_u8(out, e.src_factor);
        write_u8(out, e.dst_factor); write_u8(out, e.op);
    }
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_dither_section(const uint8_t* base, const MAT1SectionOffsets& offsets,
                           const MatInitDataSection& init, uint32_t mat1_section_size,
                           DitherSection& out)
{
    if (offsets.dither == 0) return;
    size_t pos = offsets.dither;
    out.dither.resize(init.counts.dither);
    for (auto& v : out.dither) v = read_u8(base, &pos);


    uint32_t tail = mat1_section_size - offsets.dither - init.counts.dither;
    out.padding.assign(base + pos, base + pos + tail);
}

void serialize_dither_section(std::vector<uint8_t>& out, const DitherSection& s)
{
    for (auto v : s.dither) write_u8(out, v);
    write_raw(out, s.padding.data(), s.padding.size());
}

void parse_mat1_section(const uint8_t* buf, size_t* pos_inout, MAT1Section& out)
{
    const uint8_t* base = buf + *pos_inout;
    size_t local = 0;

    local += 4;
    out.section_size   = read_be32(base, &local);
    out.material_count = read_be16(base, &local);
    out.header_padding = read_be16(base, &local);

    parse_mat1_offsets(base, &local, out.offsets);

    parse_mat_init_data_section   (base, out.offsets, out.mat_init);
    parse_mat_init_idx_section    (base, out.offsets, out.mat_init_idx);
    parse_mat_name_table_section  (base, out.offsets, out.mat_name_table);
    parse_ind_init_data_section   (base, out.offsets, out.ind_init);
    parse_cull_mode_section       (base, out.offsets, out.mat_init, out.cull_modes);
    parse_mat_color_section       (base, out.offsets, out.mat_init, out.mat_colors);
    parse_color_chan_num_section   (base, out.offsets, out.mat_init, out.color_chan_num);
    parse_color_chan_info_section  (base, out.offsets, out.mat_init, out.color_chan_info);
    parse_tex_gen_num_section      (base, out.offsets, out.mat_init, out.tex_gen_num);
    parse_tex_coord_info_section   (base, out.offsets, out.mat_init, out.tex_coord_info);
    parse_tex_mtx_info_section     (base, out.offsets, out.mat_init, out.tex_mtx_info);
    parse_tex_no_section           (base, out.offsets, out.mat_init, out.tex_no);
    parse_font_no_section          (base, out.offsets, out.mat_init, out.font_no);
    parse_tev_order_info_section   (base, out.offsets, out.mat_init, out.tev_order_info);
    parse_tev_color_section        (base, out.offsets, out.mat_init, out.tev_color);
    parse_tev_k_color_section      (base, out.offsets, out.mat_init, out.tev_k_color);
    parse_tev_stage_num_section    (base, out.offsets, out.mat_init, out.tev_stage_num);
    parse_tev_stage_info_section   (base, out.offsets, out.mat_init, out.tev_stage_info);
    parse_tev_swap_mode_info_section(base, out.offsets, out.mat_init, out.tev_swap_mode_info);
    parse_tev_swap_mode_tbl_section (base, out.offsets, out.mat_init, out.tev_swap_mode_tbl);
    parse_alpha_comp_info_section   (base, out.offsets, out.mat_init, out.alpha_comp_info);
    parse_blend_info_section        (base, out.offsets, out.mat_init, out.blend_info);
    parse_dither_section            (base, out.offsets, out.mat_init, out.section_size, out.dither);

    *pos_inout += out.section_size;
}

void serialize_mat1_section(std::vector<uint8_t>& out, const MAT1Section& s)
{

    out.insert(out.end(), {'M','A','T','1'});
    write_be32(out, s.section_size);
    write_be16(out, s.material_count);
    write_be16(out, s.header_padding);

    serialize_mat1_offsets           (out, s.offsets);
    serialize_mat_init_data_section  (out, s.mat_init);
    serialize_mat_init_idx_section   (out, s.mat_init_idx);
    serialize_mat_name_table_section (out, s.mat_name_table);
    serialize_ind_init_data_section  (out, s.ind_init);
    serialize_cull_mode_section      (out, s.cull_modes);
    serialize_mat_color_section      (out, s.mat_colors);
    serialize_color_chan_num_section  (out, s.color_chan_num);
    serialize_color_chan_info_section (out, s.color_chan_info);
    serialize_tex_gen_num_section     (out, s.tex_gen_num);
    serialize_tex_coord_info_section  (out, s.tex_coord_info);
    serialize_tex_mtx_info_section    (out, s.tex_mtx_info);
    serialize_tex_no_section          (out, s.tex_no);
    serialize_font_no_section         (out, s.font_no);
    serialize_tev_order_info_section  (out, s.tev_order_info);
    serialize_tev_color_section       (out, s.tev_color);
    serialize_tev_k_color_section     (out, s.tev_k_color);
    serialize_tev_stage_num_section   (out, s.tev_stage_num);
    serialize_tev_stage_info_section  (out, s.tev_stage_info);
    serialize_tev_swap_mode_info_section(out, s.tev_swap_mode_info);
    serialize_tev_swap_mode_tbl_section (out, s.tev_swap_mode_tbl);
    serialize_alpha_comp_info_section   (out, s.alpha_comp_info);
    serialize_blend_info_section        (out, s.blend_info);
    serialize_dither_section            (out, s.dither);
}

} // namespace Reservoir