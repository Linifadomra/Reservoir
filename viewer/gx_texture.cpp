#include "gx_texture.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

enum Format {
    I4 = 0, I8 = 1, IA4 = 2, IA8 = 3, RGB565 = 4, RGB5A3 = 5, RGBA32 = 6, C4 = 8, C8 = 9, C14X2 = 10, CMPR = 14
};

using Rgba = std::array<uint8_t, 4>;

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

Rgba rgb565(uint16_t v) {
    const uint8_t r = (v >> 11) & 31;
    const uint8_t g = (v >> 5) & 63;
    const uint8_t b = v & 31;
    return {uint8_t(r << 3 | r >> 2), uint8_t(g << 2 | g >> 4), uint8_t(b << 3 | b >> 2), 255};
}

Rgba rgb5a3(uint16_t v) {
    if (v & 0x8000) {
        const uint8_t r = (v >> 10) & 31;
        const uint8_t g = (v >> 5) & 31;
        const uint8_t b = v & 31;
        return {uint8_t(r << 3 | r >> 2), uint8_t(g << 3 | g >> 2), uint8_t(b << 3 | b >> 2), 255};
    }
    const uint8_t a = (v >> 12) & 7;
    return {uint8_t(((v >> 8) & 15) * 17), uint8_t(((v >> 4) & 15) * 17), uint8_t((v & 15) * 17), uint8_t(a << 5 | a << 2 | a >> 1)};
}

Rgba ia8(uint16_t v) {
    const uint8_t a = v >> 8;
    const uint8_t i = v & 0xFF;
    return {i, i, i, a};
}

std::vector<Rgba> decode_palette(const uint8_t* data, size_t avail, int format, int count) {
    std::vector<Rgba> out;
    for (int i = 0; i < count && size_t(i) * 2 + 2 <= avail; i++) {
        const uint16_t v = be16(data + i * 2);
        out.push_back(format == 0 ? ia8(v) : format == 1 ? rgb565(v) : rgb5a3(v));
    }
    return out;
}

struct Source {
    const uint8_t* data;
    size_t         size;
    size_t         pos = 0;

    uint8_t byte() { return pos < size ? data[pos++] : (pos++, 0); }
    uint16_t word() { const uint8_t a = byte(); return uint16_t(a << 8 | byte()); }
};

void put(DecodedImage& img, int x, int y, const Rgba& c) {
    if (x < img.width && y < img.height) {
        std::memcpy(&img.rgba[(size_t(y) * img.width + x) * 4], c.data(), 4);
    }
}

void decode_cmpr(Source& src, DecodedImage& img) {
    for (int by = 0; by < img.height; by += 8) {
        for (int bx = 0; bx < img.width; bx += 8) {
            for (int sub = 0; sub < 4; sub++) {
                const uint16_t c0 = src.word();
                const uint16_t c1 = src.word();
                Rgba           pal[4];
                pal[0] = rgb565(c0);
                pal[1] = rgb565(c1);
                if (c0 > c1) {
                    for (int k = 0; k < 3; k++) {
                        pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
                        pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
                    }
                    pal[2][3] = pal[3][3] = 255;
                } else {
                    for (int k = 0; k < 3; k++) {
                        pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
                    }
                    pal[2][3] = 255;
                    pal[3]    = {0, 0, 0, 0};
                }
                const int ox = bx + (sub & 1) * 4;
                const int oy = by + (sub >> 1) * 4;
                for (int row = 0; row < 4; row++) {
                    const uint8_t bits = src.byte();
                    for (int col = 0; col < 4; col++) {
                        put(img, ox + col, oy + row, pal[(bits >> (6 - col * 2)) & 3]);
                    }
                }
            }
        }
    }
}

void decode_rgba32(Source& src, DecodedImage& img) {
    for (int by = 0; by < img.height; by += 4) {
        for (int bx = 0; bx < img.width; bx += 4) {
            Rgba px[16];
            for (int i = 0; i < 16; i++) {
                px[i][3] = src.byte();
                px[i][0] = src.byte();
            }
            for (int i = 0; i < 16; i++) {
                px[i][1] = src.byte();
                px[i][2] = src.byte();
            }
            for (int i = 0; i < 16; i++) {
                put(img, bx + (i & 3), by + (i >> 2), px[i]);
            }
        }
    }
}

bool decode_blocks(Source& src, int format, const std::vector<Rgba>& palette, DecodedImage& img) {
    img.intensity = format == I4 || format == I8 || format == IA4 || format == IA8;
    if (format == CMPR) {
        decode_cmpr(src, img);
        return true;
    }
    if (format == RGBA32) {
        decode_rgba32(src, img);
        return true;
    }

    int bw = 4;
    int bh = 4;
    switch (format) {
    case I4:
    case C4: bw = 8; bh = 8; break;
    case I8:
    case IA4:
    case C8: bw = 8; bh = 4; break;
    case IA8:
    case RGB565:
    case RGB5A3:
    case C14X2: break;
    default: return false;
    }

    auto lookup = [&](unsigned i) { return i < palette.size() ? palette[i] : Rgba{0, 0, 0, 0}; };

    for (int by = 0; by < img.height; by += bh) {
        for (int bx = 0; bx < img.width; bx += bw) {
            for (int y = 0; y < bh; y++) {
                uint8_t carry = 0;
                for (int x = 0; x < bw; x++) {
                    Rgba c{};
                    switch (format) {
                    case I4: {
                        if ((x & 1) == 0) carry = src.byte();
                        const uint8_t v = ((x & 1) == 0 ? carry >> 4 : carry & 15) * 17;
                        c = {v, v, v, v};
                        break;
                    }
                    case I8: { const uint8_t v = src.byte(); c = {v, v, v, v}; break; }
                    case IA4: {
                        const uint8_t b = src.byte();
                        const uint8_t i = (b & 15) * 17;
                        c = {i, i, i, uint8_t((b >> 4) * 17)};
                        break;
                    }
                    case IA8: c = ia8(src.word()); break;
                    case RGB565: c = rgb565(src.word()); break;
                    case RGB5A3: c = rgb5a3(src.word()); break;
                    case C4: {
                        if ((x & 1) == 0) carry = src.byte();
                        c = lookup((x & 1) == 0 ? carry >> 4 : carry & 15);
                        break;
                    }
                    case C8: c = lookup(src.byte()); break;
                    case C14X2: c = lookup(src.word() & 0x3FFF); break;
                    }
                    put(img, bx + x, by + y, c);
                }
            }
        }
    }
    return true;
}

bool decode_tpl(const std::vector<uint8_t>& f, DecodedImage& out) {
    if (f.size() < 0x14) return false;
    const uint32_t table = be32(&f[8]);
    if (table + 8 > f.size()) return false;
    const uint32_t image = be32(&f[table]);
    const uint32_t pal   = be32(&f[table + 4]);
    if (image + 12 > f.size()) return false;

    out.height = be16(&f[image]);
    out.width  = be16(&f[image + 2]);
    const int      format = static_cast<int>(be32(&f[image + 4]));
    const uint32_t data   = be32(&f[image + 8]);
    if (out.width <= 0 || out.height <= 0 || data >= f.size()) return false;

    std::vector<Rgba> palette;
    if (pal != 0 && pal + 12 <= f.size()) {
        const int      count  = be16(&f[pal]);
        const int      pfmt   = static_cast<int>(be32(&f[pal + 4]));
        const uint32_t pdata  = be32(&f[pal + 8]);
        if (pdata < f.size()) palette = decode_palette(&f[pdata], f.size() - pdata, pfmt, count);
    }

    out.rgba.assign(size_t(out.width) * out.height * 4, 0);
    Source src{&f[data], f.size() - data};
    return decode_blocks(src, format, palette, out);
}

bool decode_bti(const std::vector<uint8_t>& f, DecodedImage& out) {
    if (f.size() < 0x20) return false;
    const int      format = f[0];
    out.width             = be16(&f[2]);
    out.height            = be16(&f[4]);
    const int      pfmt   = f[9];
    const int      count  = be16(&f[0xA]);
    const uint32_t pdata  = be32(&f[0xC]);
    const uint32_t data   = be32(&f[0x1C]);
    if (out.width <= 0 || out.height <= 0 || data >= f.size()) return false;

    std::vector<Rgba> palette;
    if (count > 0 && pdata != 0 && pdata < f.size()) palette = decode_palette(&f[pdata], f.size() - pdata, pfmt, count);

    out.rgba.assign(size_t(out.width) * out.height * 4, 0);
    Source src{&f[data], f.size() - data};
    return decode_blocks(src, format, palette, out);
}

} // namespace

bool decode_gx_texture(const std::vector<uint8_t>& file, DecodedImage& out) {
    if (file.size() >= 4 && be32(file.data()) == 0x0020AF30) {
        return decode_tpl(file, out);
    }
    return decode_bti(file, out);
}
