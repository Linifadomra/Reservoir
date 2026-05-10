#include "blo.hpp"
#include "mat1/mat1.hpp"

#include <confluence/endian.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Reservoir {

static uint8_t  rd_u8  (const uint8_t* d, size_t& p) { return d[p++]; }
static uint16_t rd_u16 (const uint8_t* d, size_t& p) { uint16_t v = gc_be16(d+p); p+=2; return v; }
static uint32_t rd_u32 (const uint8_t* d, size_t& p) { uint32_t v = gc_be32(d+p); p+=4; return v; }
static float    rd_f32 (const uint8_t* d, size_t& p) { float    v = gc_be_f32(d+p); p+=4; return v; }

static void wr_u8 (std::vector<uint8_t>& o, uint8_t  v) { o.push_back(v); }
static void wr_u16(std::vector<uint8_t>& o, uint16_t v) { o.resize(o.size()+2); gc_write_be16(o.data()+o.size()-2,v); }
static void wr_u32(std::vector<uint8_t>& o, uint32_t v) { o.resize(o.size()+4); gc_write_be32(o.data()+o.size()-4,v); }
static void wr_f32(std::vector<uint8_t>& o, float    v) { o.resize(o.size()+4); gc_write_be_f32(o.data()+o.size()-4,v); }

static void wr_tag(std::vector<uint8_t>& o, const char t[4]) {
    o.push_back(t[0]); o.push_back(t[1]); o.push_back(t[2]); o.push_back(t[3]);
}

static bool tag_eq(const uint8_t* d, size_t p, const char t[4]) {
    return std::toupper(d[p])==t[0] && std::toupper(d[p+1])==t[1] &&
           std::toupper(d[p+2])==t[2] && std::toupper(d[p+3])==t[3];
}

void read_tag(const uint8_t* d, size_t& p, uint8_t out[4]) {
    std::memcpy(out, d+p, 4); p += 4;
    for (int i = 0; i < 4; i++) out[i] = std::toupper(out[i]);
}

static INF1Section parse_inf1(const uint8_t* d, size_t& p)
{
    INF1Section s;
    s.size   = rd_u32(d, p);
    s.width  = rd_u16(d, p);
    s.height = rd_u16(d, p);
    for (int i = 0; i < 4; i++) s.values[i] = rd_u8(d, p);
    size_t pad = s.size - 16;
    s.padding.assign(d+p, d+p+pad);
    p += pad;
    return s;
}

static void serialize_inf1(const INF1Section& s, std::vector<uint8_t>& o)
{
    wr_tag(o, "INF1");
    wr_u32(o, s.size);
    wr_u16(o, s.width);
    wr_u16(o, s.height);
    for (int i = 0; i < 4; i++) wr_u8(o, s.values[i]);
    o.insert(o.end(), s.padding.begin(), s.padding.end());
}

static TEX1Section parse_tex1(const uint8_t* d, size_t& p)
{
    TEX1Section s;
    size_t section_start = p - 4;
    s.section_size  = rd_u32(d, p);
    s.texture_count = rd_u16(d, p);
    p += 2;
    s.header_size   = rd_u32(d, p);

    uint16_t offset_count = rd_u16(d, p);
    std::vector<uint16_t> offsets(offset_count);
    for (auto& ofs : offsets) ofs = rd_u16(d, p);

    for (uint16_t i = 0; i < offset_count; i++) {
        TEX1Reference ref;
        ref.res_type     = rd_u8(d, p);
        uint8_t name_len = rd_u8(d, p);
        ref.texture_name = std::string(reinterpret_cast<const char*>(d+p), name_len);
        p += name_len;
        s.references.push_back(std::move(ref));
    }

    size_t section_end = section_start + s.section_size;
    s.padding.assign(d+p, d+section_end);
    p = section_end;
    return s;
}

static void serialize_tex1(const TEX1Section& s, std::vector<uint8_t>& o)
{
    wr_tag(o, "TEX1");
    size_t sz_off = o.size(); wr_u32(o, 0);
    uint16_t count = static_cast<uint16_t>(s.references.size());
    wr_u16(o, count);
    o.push_back(0xFF); o.push_back(0xFF);
    wr_u32(o, s.header_size);
    wr_u16(o, count);

    size_t offsets_off = o.size();
    for (uint16_t i = 0; i < count; i++) wr_u16(o, 0);

    uint16_t running = static_cast<uint16_t>(count * 2 + 2);
    for (uint16_t i = 0; i < count; i++) {
        gc_write_be16(o.data() + offsets_off + i*2, running);
        running += static_cast<uint16_t>(2 + s.references[i].texture_name.size());
    }

    for (const auto& ref : s.references) {
        wr_u8(o, ref.res_type);
        wr_u8(o, static_cast<uint8_t>(ref.texture_name.size()));
        o.insert(o.end(), ref.texture_name.begin(), ref.texture_name.end());
    }
    o.insert(o.end(), s.padding.begin(), s.padding.end());
    gc_write_be32(o.data() + sz_off, static_cast<uint32_t>(s.section_size));
}

static FNT1Section parse_fnt1(const uint8_t* d, size_t& p)
{
    FNT1Section s;
    size_t section_start = p - 4;
    s.section_size = rd_u32(d, p);
    p += 2;
    p += 2;
    s.header_size  = rd_u32(d, p);

    uint16_t offset_count = rd_u16(d, p);
    for (uint16_t i = 0; i < offset_count; i++) rd_u16(d, p);

    for (uint16_t i = 0; i < offset_count; i++) {
        FNT1Reference ref;
        ref.res_type    = rd_u8(d, p);
        uint8_t name_len = rd_u8(d, p);
        ref.font_name   = std::string(reinterpret_cast<const char*>(d+p), name_len);
        p += name_len;
        s.references.push_back(std::move(ref));
    }

    size_t section_end = section_start + s.section_size;
    s.padding.assign(d+p, d+section_end);
    p = section_end;
    return s;
}

static void serialize_fnt1(const FNT1Section& s, std::vector<uint8_t>& o)
{
    wr_tag(o, "FNT1");
    size_t sz_off = o.size(); wr_u32(o, 0);
    uint16_t count = static_cast<uint16_t>(s.references.size());
    wr_u16(o, count);
    o.push_back(0xFF); o.push_back(0xFF);
    wr_u32(o, s.header_size);
    wr_u16(o, count);

    size_t offsets_off = o.size();
    for (uint16_t i = 0; i < count; i++) wr_u16(o, 0);

    uint16_t running = static_cast<uint16_t>(count * 2 + 2);
    for (uint16_t i = 0; i < count; i++) {
        gc_write_be16(o.data() + offsets_off + i*2, running);
        running += static_cast<uint16_t>(2 + s.references[i].font_name.size());
    }

    for (const auto& ref : s.references) {
        wr_u8(o, ref.res_type);
        wr_u8(o, static_cast<uint8_t>(ref.font_name.size()));
        o.insert(o.end(), ref.font_name.begin(), ref.font_name.end());
    }
    o.insert(o.end(), s.padding.begin(), s.padding.end());
    gc_write_be32(o.data() + sz_off, static_cast<uint32_t>(s.section_size));
}

static PAN2Node parse_pan2_data(const uint8_t* d, size_t& p)
{
    PAN2Node n;
    n.field_0x8    = rd_u16(d, p);
    n.bck_idx      = rd_u16(d, p);
    n.visible      = rd_u8(d, p);
    n.base_position= rd_u8(d, p);
    n.padding[0]   = rd_u8(d, p);
    n.padding[1]   = rd_u8(d, p);
    char tag[8]; std::memcpy(tag, d+p, 8); p += 8;
    n.info_tag     = std::string(tag, 8);
    char utag[8];  std::memcpy(utag, d+p, 8); p += 8;
    n.user_info_tag= std::string(utag, 8);
    n.size_x       = rd_f32(d, p);
    n.size_y       = rd_f32(d, p);
    n.scale_x      = rd_f32(d, p);
    n.scale_y      = rd_f32(d, p);
    n.rotate_x     = rd_f32(d, p);
    n.rotate_y     = rd_f32(d, p);
    n.rotate_z     = rd_f32(d, p);
    n.translate_x  = rd_f32(d, p);
    n.translate_y  = rd_f32(d, p);
    for (int i = 0; i < 4; i++) n.end_padding[i] = rd_u8(d, p);
    return n;
}

static void serialize_pan2_data(const PAN2Node& n, std::vector<uint8_t>& o)
{
    wr_u16(o, n.field_0x8);
    wr_u16(o, n.bck_idx);
    wr_u8(o, n.visible);
    wr_u8(o, n.base_position);
    wr_u8(o, n.padding[0]);
    wr_u8(o, n.padding[1]);
    char tag[8] = {}; std::memcpy(tag, n.info_tag.data(),
                                  std::min(n.info_tag.size(), size_t(8)));
    o.insert(o.end(), tag, tag+8);
    char utag[8] = {}; std::memcpy(utag, n.user_info_tag.data(),
                                   std::min(n.user_info_tag.size(), size_t(8)));
    o.insert(o.end(), utag, utag+8);
    wr_f32(o, n.size_x);    wr_f32(o, n.size_y);
    wr_f32(o, n.scale_x);   wr_f32(o, n.scale_y);
    wr_f32(o, n.rotate_x);  wr_f32(o, n.rotate_y); wr_f32(o, n.rotate_z);
    wr_f32(o, n.translate_x); wr_f32(o, n.translate_y);
    for (int i = 0; i < 4; i++) wr_u8(o, n.end_padding[i]);
}

static ElementNode parse_element(const uint8_t* d, size_t& p, size_t data_size);
static void        serialize_element(const ElementNode& n, std::vector<uint8_t>& o);

static ElementNode parse_pan2_node(const uint8_t* d, size_t& p, size_t data_size)
{
    ElementNode en;
    en.type = ElementNode::Type::PAN2;
    size_t node_start = p - 4;
    uint32_t node_size = rd_u32(d, p);
    PAN2Node pan = parse_pan2_data(d, p);

    if (tag_eq(d, p, "BGN1")) {
        p += 4;
        en.children_bgn1_tag_size = rd_u32(d, p);
        en.has_children_bgn1_tag  = true;
        while (true) {
            if (p + 4 > data_size)
                throw std::runtime_error("unexpected end of data inside PAN2 children");

            while (p % 4 != 0 && p < data_size && d[p] == 0xFF)
                ++p;

            while (p + 4 <= data_size &&
                d[p] == 0xFF && d[p+1] == 0xFF && d[p+2] == 0xFF && d[p+3] == 0xFF)
                p += 4;

            if (p + 4 > data_size)
                throw std::runtime_error("unexpected end of data inside PAN2 children");

            if (tag_eq(d, p, "END1")) {
                p += 4;
                en.end_tag_size = rd_u32(d, p);
                en.has_end_tag  = true;
                break;
            }

            if (!tag_eq(d, p, "PAN2") && !tag_eq(d, p, "PIC2") &&
                !tag_eq(d, p, "TBX2") && !tag_eq(d, p, "WIN2") &&
                !tag_eq(d, p, "BGN1")) {
                break;
            }

            pan.children.push_back(parse_element(d, p, data_size));
        }
    }

    en.node = std::move(pan);
    return en;
}

static ElementNode parse_pic2_node(const uint8_t* d, size_t& p, size_t data_size)
{
    ElementNode en;
    en.type = ElementNode::Type::PIC2;
    size_t node_start = p - 4;
    uint32_t node_size = rd_u32(d, p);
    PIC2Node pic;

    // Consume embedded 'pan2' sub-tag if present
    if (tag_eq(d, p, "PAN2")) {
        p += 4;
        pic.pan2_sub_tag_size = rd_u32(d, p);
    }

    pic.base         = parse_pan2_data(d, p);
    pic.field_0x0    = rd_u16(d, p);
    pic.field_0x2    = rd_u16(d, p);
    pic.material_num = rd_u16(d, p);
    pic.field_0x6    = rd_u16(d, p);
    for (int i = 0; i < 4; i++) pic.field_0x8[i]  = rd_u16(d, p);
    for (int i = 0; i < 8; i++) pic.field_0x10[i] = rd_u16(d, p);
    for (int i = 0; i < 4; i++) pic.corner_color[i] = rd_u32(d, p);
    size_t consumed  = p - node_start;
    size_t remaining = node_size - consumed;
    if (p + remaining > data_size)
        throw std::runtime_error("PIC2 remaining exceeds buffer");
    pic.end_padding.assign(d+p, d+p+remaining);
    p += remaining;

    en.node = std::move(pic);
    return en;
}

static ElementNode parse_tbx2_node(const uint8_t* d, size_t& p, size_t data_size)
{
    ElementNode en;
    en.type = ElementNode::Type::TBX2;
    size_t node_start = p - 4;
    uint32_t node_size = rd_u32(d, p);
    TBX2Node tbx;

    // Consume embedded 'pan2' sub-tag if present
    if (tag_eq(d, p, "PAN2")) {
        p += 4;
        tbx.pan2_sub_tag_size = rd_u32(d, p);
    }

    tbx.base         = parse_pan2_data(d, p);
    tbx.field_0x0    = rd_u16(d, p);
    tbx.field_0x2    = rd_u16(d, p);
    tbx.material_num = rd_u16(d, p);
    tbx.char_space   = rd_u16(d, p);
    tbx.line_space   = rd_u16(d, p);
    tbx.font_size_x  = rd_u16(d, p);
    tbx.font_size_y  = rd_u16(d, p);
    tbx.h_bind       = rd_u8(d, p);
    tbx.v_bind       = rd_u8(d, p);
    for (int i = 0; i < 4; i++) tbx.char_color[i] = rd_u8(d, p);
    for (int i = 0; i < 4; i++) tbx.grad_color[i] = rd_u8(d, p);
    tbx.connected    = rd_u8(d, p);
    for (int i = 0; i < 3; i++) tbx.field_0x19[i] = rd_u8(d, p);
    tbx.field_0x1c   = rd_u16(d, p);
    tbx.field_0x1e   = rd_u16(d, p);
    size_t consumed  = p - node_start;
    if (node_size < consumed)
        throw std::runtime_error("TBX2 node_size smaller than consumed bytes");
    size_t remaining = node_size - consumed;
    if (p + remaining > data_size)
        throw std::runtime_error("TBX2 remaining exceeds buffer");
    tbx.end_padding.assign(d+p, d+p+remaining);
    p += remaining;

    en.node = std::move(tbx);
    return en;
}

static ElementNode parse_win2_node(const uint8_t* d, size_t& p, size_t data_size)
{
    ElementNode en;
    en.type = ElementNode::Type::WIN2;
    size_t node_start = p - 4;
    uint32_t node_size = rd_u32(d, p);
    WIN2Node win;

    // Consume embedded 'pan2' sub-tag if present
    if (tag_eq(d, p, "PAN2")) {
        p += 4;
        win.pan2_sub_tag_size = rd_u32(d, p);
    }

    win.base = parse_pan2_data(d, p);
    size_t consumed  = p - node_start;
    if (node_size < consumed)
        throw std::runtime_error("WIN2 node_size smaller than consumed bytes");
    size_t remaining = node_size - consumed;
    if (p + remaining > data_size)
        throw std::runtime_error("WIN2 remaining exceeds buffer");
    win.data.assign(d+p, d+p+remaining);
    p += remaining;

    en.node = std::move(win);
    return en;
}

static void serialize_element(const ElementNode& en, std::vector<uint8_t>& o)
{
    switch (en.type) {
        case ElementNode::Type::PAN2: {
            const auto& pan = std::get<PAN2Node>(en.node);
            if (en.has_leading_bgn1_tag) {
                wr_tag(o, "BGN1"); wr_u32(o, en.leading_bgn1_tag_size);
            }
            wr_tag(o, "PAN2");
            wr_u32(o, 72);
            serialize_pan2_data(pan, o);
            if (en.has_children_bgn1_tag) {
                wr_tag(o, "BGN1"); wr_u32(o, en.children_bgn1_tag_size);
            }
            for (const auto& child : pan.children)
                serialize_element(child, o);
            if (en.has_end_tag) {
                wr_tag(o, "END1"); wr_u32(o, en.end_tag_size);
            }
            break;
        }
        case ElementNode::Type::PIC2: {
            const auto& pic = std::get<PIC2Node>(en.node);
            size_t tag_off = o.size();
            wr_tag(o, "PIC2");
            size_t sz_off = o.size(); wr_u32(o, 0);
            if (pic.pan2_sub_tag_size > 0) {
                wr_tag(o, "pan2"); wr_u32(o, pic.pan2_sub_tag_size);
            }
            serialize_pan2_data(pic.base, o);
            wr_u16(o, pic.field_0x0);
            wr_u16(o, pic.field_0x2);
            wr_u16(o, pic.material_num);
            wr_u16(o, pic.field_0x6);
            for (int i = 0; i < 4; i++) wr_u16(o, pic.field_0x8[i]);
            for (int i = 0; i < 8; i++) wr_u16(o, pic.field_0x10[i]);
            for (int i = 0; i < 4; i++) wr_u32(o, pic.corner_color[i]);
            o.insert(o.end(), pic.end_padding.begin(), pic.end_padding.end());
            gc_write_be32(o.data() + sz_off, static_cast<uint32_t>(o.size() - tag_off));
            break;
        }
        case ElementNode::Type::TBX2: {
            const auto& tbx = std::get<TBX2Node>(en.node);
            size_t tag_off = o.size();
            wr_tag(o, "TBX2");
            size_t sz_off = o.size(); wr_u32(o, 0);
            if (tbx.pan2_sub_tag_size > 0) {
                wr_tag(o, "pan2"); wr_u32(o, tbx.pan2_sub_tag_size);
            }
            serialize_pan2_data(tbx.base, o);
            wr_u16(o, tbx.field_0x0);
            wr_u16(o, tbx.field_0x2);
            wr_u16(o, tbx.material_num);
            wr_u16(o, tbx.char_space);
            wr_u16(o, tbx.line_space);
            wr_u16(o, tbx.font_size_x);
            wr_u16(o, tbx.font_size_y);
            wr_u8(o, tbx.h_bind);
            wr_u8(o, tbx.v_bind);
            for (int i = 0; i < 4; i++) wr_u8(o, tbx.char_color[i]);
            for (int i = 0; i < 4; i++) wr_u8(o, tbx.grad_color[i]);
            wr_u8(o, tbx.connected);
            for (int i = 0; i < 3; i++) wr_u8(o, tbx.field_0x19[i]);
            wr_u16(o, tbx.field_0x1c);
            wr_u16(o, tbx.field_0x1e);
            o.insert(o.end(), tbx.end_padding.begin(), tbx.end_padding.end());
            gc_write_be32(o.data() + sz_off, static_cast<uint32_t>(o.size() - tag_off));
            break;
        }
        case ElementNode::Type::WIN2: {
            const auto& win = std::get<WIN2Node>(en.node);
            size_t tag_off = o.size();
            wr_tag(o, "WIN2");
            size_t sz_off = o.size(); wr_u32(o, 0);
            if (win.pan2_sub_tag_size > 0) {
                wr_tag(o, "pan2"); wr_u32(o, win.pan2_sub_tag_size);
            }
            serialize_pan2_data(win.base, o);
            o.insert(o.end(), win.data.begin(), win.data.end());
            gc_write_be32(o.data() + sz_off, static_cast<uint32_t>(o.size() - tag_off));
            break;
        }
    }
}

static ElementNode parse_element(const uint8_t* d, size_t& p, size_t data_size)
{
    if (p + 4 > data_size)
        throw std::runtime_error("unexpected end of data reading element tag");

    bool had_leading_bgn1      = false;
    uint32_t leading_bgn1_size = 0;

    if (tag_eq(d, p, "BGN1")) {
        p += 4;
        if (p + 4 > data_size)
            throw std::runtime_error("unexpected end of data reading BGN1 size");
        leading_bgn1_size = rd_u32(d, p);
        had_leading_bgn1  = true;
    }

    if (p + 4 > data_size)
        throw std::runtime_error("unexpected end of data reading element magic");

    char magic[4];
    std::memcpy(magic, d+p, 4); p += 4;
    for (int i = 0; i < 4; i++) magic[i] = std::toupper((unsigned char)magic[i]);

    ElementNode en;
    if      (std::memcmp(magic, "PAN2", 4) == 0) en = parse_pan2_node(d, p, data_size);
    else if (std::memcmp(magic, "PIC2", 4) == 0) en = parse_pic2_node(d, p, data_size);
    else if (std::memcmp(magic, "TBX2", 4) == 0) en = parse_tbx2_node(d, p, data_size);
    else if (std::memcmp(magic, "WIN2", 4) == 0) en = parse_win2_node(d, p, data_size);
    else throw std::runtime_error(
        std::string("unknown element magic: ") +
        std::to_string((int8_t)magic[0]) + "," +
        std::to_string((int8_t)magic[1]) + "," +
        std::to_string((int8_t)magic[2]) + "," +
        std::to_string((int8_t)magic[3]) +
        " (0x" + [&]{
            char buf[16];
            snprintf(buf, sizeof(buf), "%02X%02X%02X%02X",
                (uint8_t)magic[0], (uint8_t)magic[1],
                (uint8_t)magic[2], (uint8_t)magic[3]);
            return std::string(buf);
        }() + ") at offset " + std::to_string(p - 4));

    if (had_leading_bgn1) {
        en.has_leading_bgn1_tag  = true;
        en.leading_bgn1_tag_size = leading_bgn1_size;
    }

    return en;
}

BLO parse_blo(const std::vector<uint8_t>& data)
{
    const uint8_t* d = data.data();
    const size_t   data_size = data.size();
    size_t p = 0;

    BLO blo;
    read_tag(d, p, blo.tag);
    std::memcpy(blo.type, d+p, 4); p += 4; 
    blo.size   = rd_u32(d, p);
    blo.blocks = rd_u32(d, p);
    std::memcpy(blo.header_padding, d+p, 16); p += 16;

    if (!tag_eq(d, p, "INF1")) throw std::runtime_error("expected INF1");
    p += 4;
    blo.inf1 = parse_inf1(d, p);

    if (!tag_eq(d, p, "TEX1")) throw std::runtime_error("expected TEX1");
    p += 4;
    blo.tex1 = parse_tex1(d, p);

    if (!tag_eq(d, p, "FNT1")) throw std::runtime_error("expected FNT1");
    p += 4;
    blo.fnt1 = parse_fnt1(d, p);

    if (!tag_eq(d, p, "MAT1")) throw std::runtime_error("expected MAT1");
    parse_mat1_section(d, &p, blo.mat1);

    blo.root = parse_element(d, p, data_size);

    if (p < data_size)
        blo.padding.assign(d+p, d+data_size);

    return blo;
}

std::vector<uint8_t> serialize_blo(const BLO& blo)
{
    std::vector<uint8_t> o;
    o.reserve(blo.size + 32);

    o.insert(o.end(), blo.tag,  blo.tag  + 4);
    o.insert(o.end(), blo.type, blo.type + 4);
    wr_u32(o, blo.size);
    wr_u32(o, blo.blocks);
    o.insert(o.end(), blo.header_padding, blo.header_padding + 16);

    serialize_inf1(blo.inf1, o);
    serialize_tex1(blo.tex1, o);
    serialize_fnt1(blo.fnt1, o);
    serialize_mat1_section(o, blo.mat1);
    serialize_element(blo.root, o);

    static constexpr uint8_t ext1[4] = {'E','X','T','1'};
    o.insert(o.end(), blo.padding.begin(), blo.padding.end());
    return o;
}

} // namespace Reservoir