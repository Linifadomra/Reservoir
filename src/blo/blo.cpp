#include "blo.hpp"
#include "mat1/mat1.hpp"
#include <cstring>
#include <stdexcept>
#include <confluence/endian.h>

using namespace Reservoir;

static void pad_to(std::vector<uint8_t>& out, size_t align) {
    size_t i = 0;
    while (out.size() % align != 0)
        out.push_back(PADDING_BYTES[i++ % sizeof(PADDING_BYTES)]);
}

/* INF1 */

static INF1Section parse_inf1(const uint8_t* d, size_t& pos) {
    INF1Section s;
    s.size   = gc_be32(d + pos); pos += 4;
    s.width  = gc_be16(d + pos); pos += 2;
    s.height = gc_be16(d + pos); pos += 2;
    for (int i = 0; i < 4; i++) s.values[i] = d[pos++];
    size_t pad_size = s.size - 16;
    s.padding.assign(d + pos, d + pos + pad_size);
    pos += pad_size;
    return s;
}

static void serialize_inf1(const INF1Section& s, std::vector<uint8_t>& out) {
    out.insert(out.end(), {'I','N','F','1'});
    size_t sz_pos = out.size();
    out.resize(out.size() + 4); gc_write_be32(out.data() + sz_pos, s.size);
    size_t w_pos = out.size();
    out.resize(out.size() + 2); gc_write_be16(out.data() + w_pos, s.width);
    size_t h_pos = out.size();
    out.resize(out.size() + 2); gc_write_be16(out.data() + h_pos, s.height);
    for (int i = 0; i < 4; i++) out.push_back(s.values[i]);
    out.insert(out.end(), s.padding.begin(), s.padding.end());
}

/* TEX1 */

static TEX1Section parse_tex1(const uint8_t* d, size_t& pos) {
    TEX1Section s;
    s.section_size    = gc_be32(d + pos); pos += 4;
    uint16_t tex_count = gc_be16(d + pos); pos += 2;
    pos += 2; 
    s.header_size     = gc_be32(d + pos); pos += 4;
    uint16_t offset_count = gc_be16(d + pos); pos += 2;

    std::vector<uint16_t> offsets(offset_count);
    for (auto& o : offsets) { o = gc_be16(d + pos); pos += 2; }

    size_t refs_start = pos;
    for (uint16_t i = 0; i < offset_count; i++) {
        TEX1Reference ref;
        ref.res_type      = d[pos++];
        uint8_t name_len  = d[pos++];
        ref.texture_name  = std::string(reinterpret_cast<const char*>(d + pos), name_len);
        pos += name_len;
        s.references.push_back(std::move(ref));
    }

    size_t section_end = refs_start - (2 + offset_count * 2) - s.header_size + s.section_size;
    while (pos < section_end) s.padding.push_back(d[pos++]);

    return s;
}

static void serialize_tex1(const TEX1Section& s, std::vector<uint8_t>& out) {
    out.insert(out.end(), {'T','E','X','1'});
    size_t sz_pos = out.size(); out.resize(out.size() + 4);
    uint16_t count = static_cast<uint16_t>(s.references.size());
    size_t c_pos = out.size(); out.resize(out.size() + 2); gc_write_be16(out.data() + c_pos, count);
    out.push_back(0xFF); out.push_back(0xFF);
    size_t hs_pos = out.size(); out.resize(out.size() + 4); gc_write_be32(out.data() + hs_pos, s.header_size);
    size_t oc_pos = out.size(); out.resize(out.size() + 2); gc_write_be16(out.data() + oc_pos, count);
    for (int i = 0; i < count; i++) {
        size_t o_pos = out.size(); out.resize(out.size() + 2);
        gc_write_be16(out.data() + o_pos, 0); // filled below
    }

    size_t first_ref = count * 2 + 2;
    size_t running = first_ref;
    size_t offsets_base = out.size() - count * 2;
    for (int i = 0; i < count; i++) {
        gc_write_be16(out.data() + offsets_base - count * 2 + i * 2,
                  static_cast<uint16_t>(i == 0 ? first_ref : running));
        if (i > 0) running += 2 + s.references[i-1].texture_name.size();
    }
    for (const auto& ref : s.references) {
        out.push_back(ref.res_type);
        out.push_back(static_cast<uint8_t>(ref.texture_name.size()));
        out.insert(out.end(), ref.texture_name.begin(), ref.texture_name.end());
    }
    out.insert(out.end(), s.padding.begin(), s.padding.end());
    gc_write_be32(out.data() + sz_pos, static_cast<uint32_t>(s.section_size));
}

/* FNT1 */

static FNT1Section parse_fnt1(const uint8_t* d, size_t& pos) {
    FNT1Section s;
    s.section_size     = gc_be32(d + pos); pos += 4;
    uint16_t fnt_count = gc_be16(d + pos); pos += 2;
    pos += 2;
    s.header_size      = gc_be32(d + pos); pos += 4;
    uint16_t offset_count = gc_be16(d + pos); pos += 2;

    std::vector<uint16_t> offsets(offset_count);
    for (auto& o : offsets) { o = gc_be16(d + pos); pos += 2; }

    size_t refs_start = pos;
    for (uint16_t i = 0; i < offset_count; i++) {
        FNT1Reference ref;
        ref.res_type    = d[pos++];
        uint8_t name_len = d[pos++];
        ref.font_name   = std::string(reinterpret_cast<const char*>(d + pos), name_len);
        pos += name_len;
        s.references.push_back(std::move(ref));
    }

    size_t section_end = refs_start - (2 + offset_count * 2) - s.header_size + s.section_size;
    while (pos < section_end) s.padding.push_back(d[pos++]);

    return s;
}

static void serialize_fnt1(const FNT1Section& s, std::vector<uint8_t>& out) {
    out.insert(out.end(), {'F','N','T','1'});
    size_t sz_pos = out.size(); out.resize(out.size() + 4);
    uint16_t count = static_cast<uint16_t>(s.references.size());
    size_t c_pos = out.size(); out.resize(out.size() + 2); gc_write_be16(out.data() + c_pos, count);
    out.push_back(0xFF); out.push_back(0xFF);
    size_t hs_pos = out.size(); out.resize(out.size() + 4); gc_write_be32(out.data() + hs_pos, s.header_size);
    size_t oc_pos = out.size(); out.resize(out.size() + 2); gc_write_be16(out.data() + oc_pos, count);
    for (int i = 0; i < count; i++) {
        size_t o_pos = out.size(); out.resize(out.size() + 2);
        gc_write_be16(out.data() + o_pos, 0);
    }
    size_t first_ref = count * 2 + 2;
    size_t running = first_ref;
    size_t offsets_base = out.size() - count * 2;
    for (int i = 0; i < count; i++) {
        gc_write_be16(out.data() + offsets_base - count * 2 + i * 2,
                  static_cast<uint16_t>(i == 0 ? first_ref : running));
        if (i > 0) running += 2 + s.references[i-1].font_name.size();
    }
    for (const auto& ref : s.references) {
        out.push_back(ref.res_type);
        out.push_back(static_cast<uint8_t>(ref.font_name.size()));
        out.insert(out.end(), ref.font_name.begin(), ref.font_name.end());
    }
    out.insert(out.end(), s.padding.begin(), s.padding.end());
    gc_write_be32(out.data() + sz_pos, static_cast<uint32_t>(s.section_size));
}