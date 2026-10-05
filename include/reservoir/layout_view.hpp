#pragma once
#ifndef RESERVOIR_LAYOUT_VIEW_H
#define RESERVOIR_LAYOUT_VIEW_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Reservoir::View {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Pane {
    std::string name;
    std::string kind;
    int         parent = -1;
    int         depth  = 0;

    bool    visible = true;
    uint8_t alpha   = 255;
    uint8_t alpha_total = 255;
    bool    inherits_alpha = false;
    uint8_t origin_x = 0;
    uint8_t origin_y = 0;

    Vec2  size;
    Vec2  translate;
    Vec2  scale{1.0f, 1.0f};
    float rotate = 0.0f;

    uint32_t color = 0;

    int                 texture = -1;
    uint32_t            black   = 0x000000FF;
    uint32_t            white   = 0xFFFFFFFF;
    uint8_t             wrap_s  = 1;
    uint8_t             wrap_t  = 1;
    std::array<Vec2, 4> corners{};
    std::array<Vec2, 4> uv{};

    std::string text;
    Vec2        font_size;

    std::vector<std::pair<std::string, std::string>> details;
};

struct Layout {
    std::string name;
    std::string format;
    float       width  = 0.0f;
    float       height = 0.0f;

    std::vector<Pane>        panes;
    std::vector<std::string> textures;
};

struct Entry {
    std::string          name;
    std::vector<uint8_t> data;
};

using Assets = std::vector<Entry>;

Layout view_layout(const std::string& name, const std::vector<uint8_t>& data);

std::vector<Entry> load_layouts(const std::filesystem::path& path);

Assets load_textures(const std::filesystem::path& path);

} // namespace Reservoir::View

#endif
