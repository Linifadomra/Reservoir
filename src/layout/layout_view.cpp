#include "reservoir/layout_view.hpp"

#include "blo/blo.hpp"
#include "brlyt/brlyt.hpp"

#include "common/endian.hpp"

#include <confluence/rarc.h>
#include <confluence/yaz0.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace Reservoir::View {

namespace {

struct Mat {
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, tx = 0.0f, ty = 0.0f;

    Mat operator*(const Mat& o) const {
        return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d,
                a * o.tx + c * o.ty + tx, b * o.tx + d * o.ty + ty};
    }

    Vec2 apply(float x, float y) const { return {a * x + c * y + tx, b * x + d * y + ty}; }
};

Mat translation(float x, float y) {
    Mat m;
    m.tx = x;
    m.ty = y;
    return m;
}

Mat rotation(float degrees) {
    const float r = degrees * 3.14159265f / 180.0f;
    Mat m;
    m.a = std::cos(r);
    m.b = std::sin(r);
    m.c = -m.b;
    m.d = m.a;
    return m;
}

Mat scaling(float x, float y) {
    Mat m;
    m.a = x;
    m.d = y;
    return m;
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string hex_color(uint32_t rgba) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%08X", rgba);
    return buf;
}

std::string number(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}

void place(Pane& pane, const Mat& m, float x0, float y0, bool flip_y, float half_w, float half_h) {
    const float x1 = x0 + pane.size.x;
    const float y1 = y0 + pane.size.y;
    const float xs[4] = {x0, x1, x1, x0};
    const float ys[4] = {y0, y0, y1, y1};
    for (int i = 0; i < 4; i++) {
        Vec2 p = m.apply(xs[i], ys[i]);
        pane.corners[i] = flip_y ? Vec2{half_w + p.x, half_h - p.y} : Vec2{half_w + p.x, half_h + p.y};
    }
}

Mat local_matrix(const Pane& pane) {
    return translation(pane.translate.x, pane.translate.y) * rotation(pane.rotate) * scaling(pane.scale.x, pane.scale.y);
}

PAN2Node& blo_base(ElementNode& node) {
    return std::visit([](auto& n) -> PAN2Node& {
        if constexpr (std::is_same_v<std::decay_t<decltype(n)>, PAN2Node>) {
            return n;
        } else {
            return n.base;
        }
    }, node.node);
}

uint32_t pack_color(uint16_t r, uint16_t g, uint16_t b, uint16_t a) {
    auto c = [](uint16_t v) { return static_cast<uint32_t>(std::clamp(static_cast<int>(static_cast<int16_t>(v)), 0, 255)); };
    return (c(r) << 24) | (c(g) << 16) | (c(b) << 8) | c(a);
}

void blo_colors(const MAT1Section& mat1, uint16_t material, Pane& pane) {
    if (material >= mat1.mat_init_idx.indexes.size()) {
        return;
    }
    const uint16_t init = mat1.mat_init_idx.indexes[material];
    if (init >= mat1.mat_init.entries.size()) {
        return;
    }
    const J2DMaterialInitData& entry = mat1.mat_init.entries[init];
    auto fetch = [&](int slot, uint32_t& out) {
        const uint16_t idx = entry.tev_color_idx[slot];
        if (idx != 0xFFFF && idx < mat1.tev_color.colors.size()) {
            const GXColorS10& c = mat1.tev_color.colors[idx];
            out = pack_color(c.r, c.g, c.b, c.a);
        }
    };
    fetch(0, pane.black);
    fetch(1, pane.white);
}

int blo_texture(const MAT1Section& mat1, uint16_t material) {
    if (material >= mat1.mat_init_idx.indexes.size()) {
        return -1;
    }
    const uint16_t init = mat1.mat_init_idx.indexes[material];
    if (init >= mat1.mat_init.entries.size()) {
        return -1;
    }
    const uint16_t slot = mat1.mat_init.entries[init].tex_no_idx[0];
    if (slot == 0xFFFF || slot >= mat1.tex_no.tex_no.size()) {
        return -1;
    }
    return mat1.tex_no.tex_no[slot];
}

void blo_walk(ElementNode& node, const MAT1Section& mat1, int parent, int depth, const Mat& parent_mat, Layout& out) {
    PAN2Node& base = blo_base(node);

    Pane pane;
    static const char* kKinds[] = {"PAN2", "PIC2", "TBX2", "WIN2"};
    pane.kind     = kKinds[static_cast<int>(node.type)];
    pane.name     = base.info_tag;
    pane.parent   = parent;
    pane.depth    = depth;
    pane.visible  = base.visible != 0;
    pane.origin_x = base.base_position % 3;
    pane.origin_y = base.base_position / 3;
    pane.size     = {base.size_x, base.size_y};
    pane.translate = {base.translate_x, base.translate_y};
    pane.scale    = {base.scale_x, base.scale_y};
    pane.rotate   = base.rotate_z;

    pane.uv = {Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}};

    if (node.type == ElementNode::Type::PIC2) {
        pane.texture = blo_texture(mat1, std::get<PIC2Node>(node.node).material_num);
        blo_colors(mat1, std::get<PIC2Node>(node.node).material_num, pane);

        const PIC2Node& pic = std::get<PIC2Node>(node.node);
        auto            at  = [&](int i) { return Vec2{pic.field_0x10[i * 2] / 256.0f, pic.field_0x10[i * 2 + 1] / 256.0f}; };
        pane.uv             = {at(0), at(1), at(3), at(2)};
        pane.color = std::get<PIC2Node>(node.node).corner_color[0];
        pane.details.push_back({"Corner colors", hex_color(pane.color) + " " + hex_color(std::get<PIC2Node>(node.node).corner_color[1]) + " " +
                                                      hex_color(std::get<PIC2Node>(node.node).corner_color[2]) + " " +
                                                      hex_color(std::get<PIC2Node>(node.node).corner_color[3])});
    } else if (node.type == ElementNode::Type::TBX2) {
        const TBX2Node& tbx = std::get<TBX2Node>(node.node);
        pane.font_size = {static_cast<float>(tbx.font_size_x), static_cast<float>(tbx.font_size_y)};
        pane.color = (uint32_t(tbx.char_color[0]) << 24) | (uint32_t(tbx.char_color[1]) << 16) | (uint32_t(tbx.char_color[2]) << 8) | tbx.char_color[3];
        pane.details.push_back({"Font size", number(tbx.font_size_x) + " x " + number(tbx.font_size_y)});
        pane.details.push_back({"Char / line space", std::to_string(tbx.char_space) + " / " + std::to_string(tbx.line_space)});
        pane.details.push_back({"Text color", hex_color(pane.color)});
    }
    pane.details.push_back({"User info", base.user_info_tag});
    pane.details.push_back({"Animation index", std::to_string(base.bck_idx)});

    const float ox = pane.origin_x * 0.5f * pane.size.x;
    const float oy = pane.origin_y * 0.5f * pane.size.y;
    const Mat   m  = parent_mat * local_matrix(pane);
    place(pane, m, -ox, -oy, false, 0.0f, 0.0f);

    const int index = static_cast<int>(out.panes.size());
    out.panes.push_back(std::move(pane));

    for (ElementNode& child : base.children) {
        blo_walk(child, mat1, index, depth + 1, m, out);
    }
}

Layout view_blo(const std::string& name, const std::vector<uint8_t>& data) {
    BLO blo = parse_blo(data);

    Layout out;
    out.name   = name;
    out.format = "BLO";
    out.width  = blo.inf1.width;
    out.height = blo.inf1.height;
    for (const TEX1Reference& ref : blo.tex1.references) {
        out.textures.push_back(ref.texture_name);
    }

    blo_walk(blo.root, blo.mat1, -1, 0, Mat{}, out);
    return out;
}

float read_float(const uint8_t* p, bool le) {
    const uint32_t bits = Endian::readU32(p, le);
    float          v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

struct BrlytMaterial {
    int      texture = -1;
    uint32_t black   = 0x000000FF;
    uint32_t white   = 0xFFFFFFFF;
    uint8_t  wrap_s  = 1;
    uint8_t  wrap_t  = 1;
    bool     has_srt = false;
    float    srt[5]  = {0, 0, 0, 1, 1};
};

void apply_srt(Pane& pane, const BrlytMaterial& material) {
    if (!material.has_srt) {
        return;
    }
    const float r  = material.srt[2] * 3.14159265f / 180.0f;
    const float cs = std::cos(r);
    const float sn = std::sin(r);
    for (Vec2& uv : pane.uv) {
        const float x = (uv.x - 0.5f) * material.srt[3];
        const float y = (uv.y - 0.5f) * material.srt[4];
        uv.x          = x * cs - y * sn + 0.5f + material.srt[0];
        uv.y          = x * sn + y * cs + 0.5f + material.srt[1];
    }
}

void apply_material(const std::vector<BrlytMaterial>& materials, uint16_t index, Pane& pane) {
    if (index < materials.size()) {
        pane.texture = materials[index].texture;
        pane.black   = materials[index].black;
        pane.white   = materials[index].white;
        pane.wrap_s  = materials[index].wrap_s;
        pane.wrap_t  = materials[index].wrap_t;
    }
}

std::string utf16_text(const uint8_t* p, size_t bytes, bool le) {
    std::string out;
    for (size_t i = 0; i + 1 < bytes; i += 2) {
        const uint16_t c = Endian::readU16(p + i, le);
        if (c == 0) {
            break;
        }
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

Layout view_brlyt(const std::string& name, const std::vector<uint8_t>& data) {
    const Brlyt::Document doc = Brlyt::parse(data);
    if (doc.magic != "RLYT") {
        throw std::runtime_error("not a layout (RLYT) file");
    }

    Layout out;
    out.name   = name;
    out.format = "BRLYT";

    std::vector<BrlytMaterial> materials;
    std::vector<int> stack;
    int              last_pane = -1;
    int              depth     = 0;

    std::vector<Mat> frame(1);

    for (const Brlyt::Chunk& chunk : doc.chunks) {
        const bool le = chunk.littleEndian;
        const uint8_t* b = chunk.body.data();

        if (chunk.tag == "lyt1" && chunk.body.size() >= 12) {
            uint32_t w = Endian::readU32(b + 4, le);
            uint32_t h = Endian::readU32(b + 8, le);
            std::memcpy(&out.width, &w, 4);
            std::memcpy(&out.height, &h, 4);
        } else if (chunk.tag == "mat1" && chunk.body.size() >= 4) {
            const uint16_t count = Endian::readU16(b, le);
            for (uint16_t i = 0; i < count; i++) {
                const size_t entry = 4 + size_t(i) * 4;
                if (entry + 4 > chunk.body.size()) {
                    break;
                }
                const size_t off = Endian::readU32(b + entry, le);
                BrlytMaterial material;
                if (off >= Brlyt::kChunkHeaderSize && off - Brlyt::kChunkHeaderSize + 0x44 <= chunk.body.size()) {
                    const size_t   m     = off - Brlyt::kChunkHeaderSize;
                    const uint32_t flags = Endian::readU32(b + m + 0x3C, le);
                    if ((flags & 3) > 0) {
                        material.texture = Endian::readU16(b + m + 0x40, le);
                        material.wrap_s  = b[m + 0x42];
                        material.wrap_t  = b[m + 0x43];
                    }
                    auto color = [&](size_t at) {
                        return pack_color(Endian::readU16(b + at, le), Endian::readU16(b + at + 2, le), Endian::readU16(b + at + 4, le),
                                          Endian::readU16(b + at + 6, le));
                    };
                    const uint32_t srt_count = (flags >> 4) & 3;
                    const size_t   srt_at    = m + 0x40 + size_t(flags & 3) * 4;
                    if (srt_count > 0 && srt_at + 20 <= chunk.body.size()) {
                        material.has_srt = true;
                        for (int k = 0; k < 5; k++) {
                            material.srt[k] = read_float(b + srt_at + k * 4, le);
                        }
                    }
                    material.black = color(m + 0x14);
                    material.white = color(m + 0x1C);
                }
                materials.push_back(material);
            }
        } else if (chunk.tag == "txl1" && chunk.body.size() >= 8) {
            const uint16_t count = Endian::readU16(b, le);
            const uint32_t base  = 4;
            for (uint16_t i = 0; i < count; i++) {
                const size_t entry = base + size_t(i) * 8;
                if (entry + 4 > chunk.body.size()) {
                    break;
                }
                const size_t off = base + Endian::readU32(b + entry, le);
                if (off < chunk.body.size()) {
                    out.textures.emplace_back(reinterpret_cast<const char*>(b + off));
                }
            }
        } else if (Brlyt::isPaneTag(chunk.tag) && chunk.body.size() >= Brlyt::kPaneSizeOffset - Brlyt::kChunkHeaderSize + 8) {
            Pane pane;
            pane.kind     = chunk.tag;
            pane.name     = Brlyt::paneName(chunk);
            pane.parent   = depth > 0 && !stack.empty() ? stack.back() : -1;
            pane.depth    = depth;
            pane.visible  = (b[0] & 1) != 0;
            pane.origin_x = b[1] % 3;
            pane.origin_y = b[1] / 3;
            pane.alpha    = b[2];
            pane.inherits_alpha = (b[0] & 2) != 0;
            pane.translate = {Brlyt::getPaneFloat(chunk, 0x24), Brlyt::getPaneFloat(chunk, 0x28)};
            pane.rotate    = Brlyt::getPaneFloat(chunk, 0x38);
            pane.scale     = {Brlyt::getPaneFloat(chunk, 0x3C), Brlyt::getPaneFloat(chunk, 0x40)};
            pane.size      = {Brlyt::getPaneFloat(chunk, 0x44), Brlyt::getPaneFloat(chunk, 0x48)};

            pane.details.push_back({"Flags", std::to_string(b[0])});
            pane.details.push_back({"Translate Z", number(Brlyt::getPaneFloat(chunk, 0x2C))});

            const size_t header = Brlyt::kPaneSizeOffset - Brlyt::kChunkHeaderSize + 8;
            pane.uv = {Vec2{0, 1}, Vec2{1, 1}, Vec2{1, 0}, Vec2{0, 0}};

            if (chunk.tag == "pic1" && chunk.body.size() >= header + 20) {
                pane.color = Endian::readU32(b + header, le);
                pane.details.push_back({"Corner colors", hex_color(pane.color) + " " + hex_color(Endian::readU32(b + header + 4, le)) + " " +
                                                              hex_color(Endian::readU32(b + header + 8, le)) + " " +
                                                              hex_color(Endian::readU32(b + header + 12, le))});
                apply_material(materials, Endian::readU16(b + header + 16, le), pane);

                const uint8_t sets = b[header + 18];
                if (sets > 0 && chunk.body.size() >= header + 20 + 32) {
                    const uint8_t* t = b + header + 20;
                    auto at = [&](int i) { return Vec2{read_float(t + i * 8, le), read_float(t + i * 8 + 4, le)}; };
                    pane.uv = {at(2), at(3), at(1), at(0)};
                }
                if (const uint16_t index = Endian::readU16(b + header + 16, le); index < materials.size()) {
                    apply_srt(pane, materials[index]);
                }
            } else if (chunk.tag == "txt1" && chunk.body.size() >= header + 0x28) {
                const uint32_t str_off = Endian::readU32(b + header + 12, le);
                pane.color     = Endian::readU32(b + header + 16, le);
                pane.font_size = {read_float(b + header + 0x18, le), read_float(b + header + 0x1C, le)};
                pane.details.push_back({"Text color", hex_color(pane.color)});
                pane.details.push_back({"Font size", number(pane.font_size.x) + " x " + number(pane.font_size.y)});
                if (str_off >= Brlyt::kChunkHeaderSize && str_off - Brlyt::kChunkHeaderSize < chunk.body.size()) {
                    const size_t at = str_off - Brlyt::kChunkHeaderSize;
                    pane.text = utf16_text(b + at, chunk.body.size() - at, le);
                    pane.details.push_back({"Text", pane.text});
                }
            }

            const Mat parent = frame[static_cast<size_t>(depth)];
            const Mat m      = parent * local_matrix(pane);
            const float ox   = pane.origin_x * 0.5f;
            const float oy   = pane.origin_y * 0.5f;
            const float y0   = -(1.0f - oy) * pane.size.y;
            place(pane, m, -ox * pane.size.x, y0, true, out.width * 0.5f, out.height * 0.5f);

            pane.alpha_total = pane.alpha;
            if (pane.inherits_alpha && pane.parent >= 0) {
                pane.alpha_total = static_cast<uint8_t>(pane.alpha * out.panes[pane.parent].alpha_total / 255);
            }

            last_pane = static_cast<int>(out.panes.size());
            out.panes.push_back(std::move(pane));
            if (frame.size() <= static_cast<size_t>(depth) + 1) {
                frame.resize(static_cast<size_t>(depth) + 2);
            }
            frame[static_cast<size_t>(depth) + 1] = m;
        } else if (chunk.tag == "pas1") {
            stack.push_back(last_pane);
            depth++;
        } else if (chunk.tag == "pae1") {
            if (!stack.empty()) {
                stack.pop_back();
            }
            depth = std::max(0, depth - 1);
        }
    }

    return out;
}

std::vector<uint8_t> decompress(const uint8_t* data, size_t size) {
    if (gc_yaz0_is_compressed(data, size)) {
        uint8_t* out   = nullptr;
        size_t   out_n = 0;
        if (gc_yaz0_decompress(data, size, &out, &out_n) != 0) {
            throw std::runtime_error("yaz0 decompress failed");
        }
        std::vector<uint8_t> result(out, out + out_n);
        std::free(out);
        return result;
    }
    return {data, data + size};
}

std::vector<uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        throw std::runtime_error("cannot open: " + p.string());
    }
    return {std::istreambuf_iterator<char>(f), {}};
}

bool is_layout_name(const std::string& name) {
    const std::string l = lower(name);
    return l.ends_with(".blo") || l.ends_with(".brlyt");
}

void load_arc(const fs::path& path, const std::string& prefix, std::vector<Entry>& out) {
    const std::vector<uint8_t> raw = read_file(path);
    const std::vector<uint8_t> arc_bytes = decompress(raw.data(), raw.size());
    if (arc_bytes.size() >= 4 && std::memcmp(arc_bytes.data(), "CRAR", 4) == 0) {
        throw std::runtime_error(path.filename().string() + " is a byte-swapped PC archive. Open the original, unswapped copy instead.");
    }
    GCArc* arc = gc_arc_open_mem(arc_bytes.data(), arc_bytes.size());
    if (!arc) {
        throw std::runtime_error("not a valid archive: " + path.filename().string());
    }

    const int count = gc_arc_entry_count(arc);
    for (int i = 0; i < count; ++i) {
        const GCEntry* entry = gc_arc_entry(arc, i);
        if (!entry || !entry->name || !is_layout_name(entry->name)) {
            continue;
        }
        void*  file = nullptr;
        size_t size = 0;
        if (gc_arc_read_file(arc, i, &file, &size) != 0) {
            continue;
        }
        Entry e;
        e.name = prefix + entry->name;
        e.data = decompress(static_cast<const uint8_t*>(file), size);
        std::free(file);
        out.push_back(std::move(e));
    }
    gc_arc_close(arc);
}

bool is_texture_name(const std::string& name) {
    const std::string l = lower(name);
    return l.ends_with(".tpl") || l.ends_with(".bti");
}

std::string base_name(const std::string& name) {
    const size_t slash = name.find_last_of("/\\");
    return lower(slash == std::string::npos ? name : name.substr(slash + 1));
}

void load_arc_textures(const fs::path& path, const std::string& prefix, Assets& out) {
    const std::vector<uint8_t> raw       = read_file(path);
    const std::vector<uint8_t> arc_bytes = decompress(raw.data(), raw.size());
    GCArc* arc = gc_arc_open_mem(arc_bytes.data(), arc_bytes.size());
    if (!arc) {
        return;
    }
    const int count = gc_arc_entry_count(arc);
    for (int i = 0; i < count; ++i) {
        const GCEntry* entry = gc_arc_entry(arc, i);
        if (!entry || !entry->name || !is_texture_name(entry->name)) {
            continue;
        }
        void*  file = nullptr;
        size_t size = 0;
        if (gc_arc_read_file(arc, i, &file, &size) != 0) {
            continue;
        }
        out.push_back({prefix + base_name(entry->name), decompress(static_cast<const uint8_t*>(file), size)});
        std::free(file);
    }
    gc_arc_close(arc);
}

} // namespace

Assets load_textures(const fs::path& path) {
    Assets out;
    std::vector<fs::path> files;
    const bool            folder = fs::is_directory(path);
    if (folder) {
        for (const auto& e : fs::recursive_directory_iterator(path)) {
            if (e.is_regular_file()) {
                files.push_back(e.path());
            }
        }
        std::sort(files.begin(), files.end());
    } else {
        files.push_back(path);
    }

    for (const fs::path& f : files) {
        const std::string ext = lower(f.extension().string());
        if (ext == ".arc" || ext == ".szs" || ext == ".yaz0") {
            try {
                load_arc_textures(f, folder ? f.filename().string() + "/" : "", out);
            } catch (const std::exception&) {
            }
        } else if (is_texture_name(f.filename().string())) {
            out.push_back({base_name(f.filename().string()), read_file(f)});
        }
    }
    return out;
}

Layout view_layout(const std::string& name, const std::vector<uint8_t>& data) {
    if (data.size() >= 4 && (std::memcmp(data.data(), "RLYT", 4) == 0 || std::memcmp(data.data(), "TYLR", 4) == 0)) {
        return view_brlyt(name, data);
    }
    return view_blo(name, data);
}

std::vector<Entry> load_layouts(const fs::path& path) {
    std::vector<Entry> out;

    if (fs::is_directory(path)) {
        std::vector<fs::path> files;
        for (const auto& e : fs::recursive_directory_iterator(path)) {
            if (e.is_regular_file()) {
                files.push_back(e.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files) {
            const std::string ext = lower(f.extension().string());
            if (ext == ".arc") {
                try {
                    load_arc(f, f.filename().string() + "/", out);
                } catch (const std::exception&) {
                }
            } else if (is_layout_name(f.filename().string())) {
                out.push_back({f.filename().string(), read_file(f)});
            }
        }
        return out;
    }

    const std::string ext = lower(path.extension().string());
    if (ext == ".arc" || ext == ".szs" || ext == ".yaz0") {
        load_arc(path, "", out);
    } else {
        out.push_back({path.filename().string(), read_file(path)});
    }
    return out;
}

} // namespace Reservoir::View
