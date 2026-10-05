#include "gx_texture.hpp"
#include "reservoir/layout_view.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <map>
#include <tuple>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using Reservoir::View::Entry;
using Reservoir::View::Layout;
using Reservoir::View::Pane;

namespace {

struct GpuTexture {
    SDL_Texture* handle = nullptr;
    int          width  = 0;
    int          height = 0;
    bool         found  = false;
    bool         intensity = false;
};

struct Loaded {
    ~Loaded() {
        for (GpuTexture& t : textures) {
            if (t.handle) SDL_DestroyTexture(t.handle);
        }
        for (auto& baked : tinted) {
            if (baked.second) SDL_DestroyTexture(baked.second);
        }
    }

    std::vector<DecodedImage> images;
    std::map<std::tuple<int, uint32_t, uint32_t>, SDL_Texture*> tinted;

    Layout                  layout;
    std::vector<GpuTexture> textures;
    std::vector<bool> shown;
    std::vector<std::vector<int>> children;
    std::vector<int>  roots;
};

struct App {
    SDL_Renderer*                    renderer = nullptr;
    Reservoir::View::Assets          assets;
    std::vector<Entry>               entries;
    std::vector<std::unique_ptr<Loaded>> cache;
    std::string                      source;
    std::string                      error;

    int   entry     = -1;
    int   selected  = -1;
    int   hovered   = -1;
    int   scrollTo  = -1;

    ImVec2 pan{0, 0};
    float  zoom     = 1.0f;
    bool   needFit  = true;
    bool   userMoved = false;
    ImVec2 lastAvail{0, 0};

    bool showLabels  = true;
    bool showHidden  = false;
    bool showGrid    = true;
    bool showFill    = true;
    bool showFrame   = true;
    bool showTextures = true;
    bool showText    = true;
    bool useAlpha    = false;

    char layoutFilter[96] = {};
    char paneFilter[96]   = {};
    char openPath[1024]   = {};
    bool openPopup        = false;
};

std::string lowered(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool contains(const std::string& haystack, const char* needle) {
    return needle[0] == '\0' || lowered(haystack).find(lowered(needle)) != std::string::npos;
}

ImU32 kindColor(const std::string& kind, int alpha) {
    const std::string k = lowered(kind);
    if (k == "pic2" || k == "pic1") return IM_COL32(110, 200, 130, alpha);
    if (k == "tbx2" || k == "txt1") return IM_COL32(240, 170, 90, alpha);
    if (k == "win2" || k == "wnd1") return IM_COL32(190, 130, 240, alpha);
    if (k == "bnd1") return IM_COL32(90, 200, 210, alpha);
    return IM_COL32(130, 160, 220, alpha);
}

const char* kindLabel(const std::string& kind) {
    const std::string k = lowered(kind);
    if (k == "pic2" || k == "pic1") return "Picture";
    if (k == "tbx2" || k == "txt1") return "Text";
    if (k == "win2" || k == "wnd1") return "Window";
    if (k == "bnd1") return "Bounding";
    return "Pane";
}

void loadSource(App& app, const std::string& path) {
    app.error.clear();
    try {
        std::vector<Entry> entries = Reservoir::View::load_layouts(path);
        if (entries.empty()) {
            app.error = "No layouts found in " + path;
            return;
        }
        app.entries = std::move(entries);
        app.assets  = Reservoir::View::load_textures(path);
        app.cache.clear();
        app.cache.resize(app.entries.size());
        app.source   = path;
        app.entry    = 0;
        app.selected = -1;
        app.needFit  = true;
    } catch (const std::exception& e) {
        app.error = e.what();
    }
}

std::string baseName(const std::string& name) {
    const size_t slash = name.find_last_of("/\\");
    return lowered(slash == std::string::npos ? name : name.substr(slash + 1));
}

SDL_Texture* paneTexture(App& app, Loaded& data, const Pane& pane) {
    if (pane.texture < 0 || pane.texture >= static_cast<int>(data.textures.size())) return nullptr;
    const GpuTexture& tex = data.textures[pane.texture];
    if (!tex.handle) return nullptr;

    const uint32_t black = pane.black & 0xFFFFFF00u;
    const uint32_t white = pane.white;
    if (!tex.intensity || (black == 0 && white == 0xFFFFFFFFu)) return tex.handle;

    const auto key = std::make_tuple(pane.texture, black, white);
    auto       it  = data.tinted.find(key);
    if (it != data.tinted.end()) return it->second;

    const DecodedImage& src = data.images[pane.texture];
    std::vector<uint8_t> pixels(src.rgba.size());
    for (size_t i = 0; i < src.rgba.size(); i += 4) {
        const int v = src.rgba[i];
        for (int c = 0; c < 3; c++) {
            const int lo = (black >> (24 - 8 * c)) & 0xFF;
            const int hi = (white >> (24 - 8 * c)) & 0xFF;
            pixels[i + c] = static_cast<uint8_t>(lo + (hi - lo) * v / 255);
        }
        pixels[i + 3] = static_cast<uint8_t>(src.rgba[i + 3] * (white & 0xFF) / 255);
    }

    SDL_Texture* baked = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, src.width, src.height);
    if (baked) {
        SDL_UpdateTexture(baked, nullptr, pixels.data(), src.width * 4);
        SDL_SetTextureBlendMode(baked, SDL_BLENDMODE_BLEND);
    }
    data.tinted[key] = baked;
    return baked;
}

void uploadTextures(App& app, Loaded& data, const std::string& entryName) {
    std::string prefix;
    const size_t arc = entryName.find(".arc/");
    if (arc != std::string::npos) prefix = entryName.substr(0, arc + 5);

    data.textures.resize(data.layout.textures.size());
    data.images.resize(data.layout.textures.size());
    for (size_t i = 0; i < data.textures.size(); i++) {
        const std::string wanted = prefix + baseName(data.layout.textures[i]);
        for (const Entry& asset : app.assets) {
            if (asset.name != wanted) continue;
            DecodedImage image;
            if (!decode_gx_texture(asset.data, image)) break;
            SDL_Texture* tex = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
            if (!tex) break;
            SDL_UpdateTexture(tex, nullptr, image.rgba.data(), image.width * 4);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            data.textures[i] = {tex, image.width, image.height, true, image.intensity};
            data.images[i]   = std::move(image);
            break;
        }
    }
}

Loaded* current(App& app) {
    if (app.entry < 0 || app.entry >= static_cast<int>(app.entries.size())) {
        return nullptr;
    }
    std::unique_ptr<Loaded>& slot = app.cache[app.entry];
    if (!slot) {
        slot = std::make_unique<Loaded>();
        try {
            slot->layout = Reservoir::View::view_layout(app.entries[app.entry].name, app.entries[app.entry].data);
            uploadTextures(app, *slot, app.entries[app.entry].name);
            const std::vector<Pane>& panes = slot->layout.panes;
            slot->children.resize(panes.size());
            slot->shown.assign(panes.size(), true);
            for (size_t i = 0; i < panes.size(); i++) {
                if (panes[i].parent >= 0) {
                    slot->children[panes[i].parent].push_back(static_cast<int>(i));
                    slot->shown[i] = panes[i].visible && slot->shown[panes[i].parent];
                } else {
                    slot->roots.push_back(static_cast<int>(i));
                    slot->shown[i] = panes[i].visible;
                }
            }
        } catch (const std::exception& e) {
            app.error = app.entries[app.entry].name + ": " + e.what();
        }
    }
    return slot.get();
}

void fitView(App& app, const Layout& layout, ImVec2 size) {
    if (layout.width <= 0 || layout.height <= 0 || size.x <= 0 || size.y <= 0) {
        return;
    }
    app.zoom = std::min(size.x / (layout.width * 1.15f), size.y / (layout.height * 1.15f));
    app.pan  = ImVec2(-layout.width * 0.5f * app.zoom, -layout.height * 0.5f * app.zoom);
    app.needFit = false;
}

bool inQuad(const std::array<ImVec2, 4>& q, ImVec2 p) {
    bool pos = false;
    bool neg = false;
    for (int i = 0; i < 4; i++) {
        const ImVec2 a = q[i];
        const ImVec2 b = q[(i + 1) % 4];
        const float  c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        pos |= c > 0;
        neg |= c < 0;
    }
    return !(pos && neg);
}

void drawTree(App& app, Loaded& data, int index) {
    const Pane& pane = data.layout.panes[index];
    const bool  leaf = data.children[index].empty();
    const bool  dim  = !data.shown[index];

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (index == app.selected) flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushStyleColor(ImGuiCol_Text, dim ? ImVec4(0.5f, 0.5f, 0.55f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::PushID(index);
    if (index == app.scrollTo) {
        ImGui::SetScrollHereY(0.5f);
        app.scrollTo = -1;
    }
    const bool open = ImGui::TreeNodeEx("##n", flags, "%s", "");
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) app.selected = index;
    ImGui::SameLine();
    ImGui::TextColored(ImColor(kindColor(pane.kind, 255)), "%s", kindLabel(pane.kind));
    ImGui::SameLine();
    ImGui::TextUnformatted(pane.name.empty() ? "(unnamed)" : pane.name.c_str());
    ImGui::PopID();
    ImGui::PopStyleColor();

    if (open && !leaf) {
        for (int child : data.children[index]) drawTree(app, data, child);
        ImGui::TreePop();
    }
}

void drawLeftPanel(App& app, Loaded* data, ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("Layouts", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    if (app.entries.size() > 1) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##lf", "Filter layouts", app.layoutFilter, sizeof(app.layoutFilter));
        const float h = std::min(180.0f, ImGui::GetContentRegionAvail().y * 0.35f);
        if (ImGui::BeginChild("##entries", ImVec2(0, h), ImGuiChildFlags_Borders)) {
            for (int i = 0; i < static_cast<int>(app.entries.size()); i++) {
                if (!contains(app.entries[i].name, app.layoutFilter)) continue;
                if (ImGui::Selectable(app.entries[i].name.c_str(), i == app.entry)) {
                    app.entry    = i;
                    app.selected = -1;
                    app.needFit  = true;
                }
            }
        }
        ImGui::EndChild();
    }

    ImGui::SeparatorText("Panes");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##pf", "Search panes", app.paneFilter, sizeof(app.paneFilter));

    if (ImGui::BeginChild("##tree")) {
        if (data) {
            if (app.paneFilter[0] != '\0') {
                for (int i = 0; i < static_cast<int>(data->layout.panes.size()); i++) {
                    if (!contains(data->layout.panes[i].name, app.paneFilter)) continue;
                    ImGui::PushID(i);
                    if (ImGui::Selectable(data->layout.panes[i].name.c_str(), i == app.selected)) app.selected = i;
                    ImGui::PopID();
                }
            } else {
                for (int root : data->roots) drawTree(app, *data, root);
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void row(const char* key, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", key);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", value.c_str());
}

std::string fmt(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

void drawInspector(App& app, Loaded* data, ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    if (data) {
        const Layout& l = data->layout;
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", l.name.c_str());
        ImGui::TextDisabled("%s  |  %.0f x %.0f  |  %zu panes", l.format.c_str(), l.width, l.height, l.panes.size());

        if (!l.textures.empty() && ImGui::CollapsingHeader("Textures")) {
            for (size_t i = 0; i < l.textures.size(); i++) {
                const GpuTexture& t = data->textures[i];
                if (t.found) {
                    ImGui::BulletText("%s  (%d x %d)", l.textures[i].c_str(), t.width, t.height);
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        const float k = std::min(1.0f, 256.0f / std::max(t.width, t.height));
                        ImGui::Image(reinterpret_cast<ImTextureID>(t.handle), ImVec2(t.width * k, t.height * k));
                        ImGui::EndTooltip();
                    }
                } else {
                    ImGui::BulletText("%s", l.textures[i].c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("(not found)");
                }
            }
        }
        ImGui::Separator();

        if (app.selected >= 0 && app.selected < static_cast<int>(l.panes.size())) {
            const Pane& p = l.panes[app.selected];
            ImGui::TextColored(ImColor(kindColor(p.kind, 255)), "%s", kindLabel(p.kind));
            ImGui::SameLine();
            ImGui::Text("%s", p.name.empty() ? "(unnamed)" : p.name.c_str());
            if (p.parent >= 0 && ImGui::SmallButton("Go to parent")) {
                app.selected = p.parent;
                app.scrollTo = p.parent;
            }

            if (ImGui::BeginTable("props", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("v");
                row("Type", p.kind);
                row("Visible", p.visible ? (data->shown[app.selected] ? "yes" : "yes (parent hidden)") : "no");
                row("Size", fmt(p.size.x) + " x " + fmt(p.size.y));
                row("Translate", fmt(p.translate.x) + ", " + fmt(p.translate.y));
                row("Scale", fmt(p.scale.x) + ", " + fmt(p.scale.y));
                row("Rotate Z", fmt(p.rotate) + " deg");
                static const char* hs[] = {"left", "center", "right"};
                static const char* vs[] = {"top", "center", "bottom"};
                row("Origin", std::string(vs[p.origin_y % 3]) + " " + hs[p.origin_x % 3]);
                row("Alpha", std::to_string(p.alpha));
                row("Children", std::to_string(data->children[app.selected].size()));
                for (const auto& d : p.details) row(d.first.c_str(), d.second);
                ImGui::EndTable();
            }

            if (p.texture >= 0 && p.texture < static_cast<int>(data->textures.size())) {
                const GpuTexture& t = data->textures[p.texture];
                ImGui::SeparatorText("Texture");
                ImGui::TextUnformatted(l.textures[p.texture].c_str());
                if (t.handle) {
                    const float k = std::min(1.0f, ImGui::GetContentRegionAvail().x / t.width);
                    ImGui::Image(reinterpret_cast<ImTextureID>(t.handle), ImVec2(t.width * k, t.height * k));
                } else {
                    ImGui::TextDisabled("not found in the archive");
                }
            }
        } else {
            ImGui::TextDisabled("Select a pane in the tree or on the canvas.");
        }
    } else {
        ImGui::TextDisabled("Drop a layout archive, .blo or .brlyt\nonto the window to open it.");
    }
    ImGui::End();
}

struct Segment {
    float t0, t1, ua, ub;
};

std::vector<Segment> segments(float u0, float u1, int wrap) {
    std::vector<Segment> out;
    const float span = u1 - u0;
    if (std::abs(span) < 1e-5f) {
        const float u = wrap == 0 ? std::clamp(u0, 0.0f, 1.0f) : u0 - std::floor(u0);
        out.push_back({0.0f, 1.0f, u, u});
        return out;
    }

    std::vector<float> cuts{u0, u1};
    const float        lo = std::min(u0, u1);
    const float        hi = std::max(u0, u1);
    if (wrap == 0) {
        for (float edge : {0.0f, 1.0f}) {
            if (edge > lo && edge < hi) cuts.push_back(edge);
        }
    } else {
        int count = 0;
        for (float edge = std::floor(lo) + 1.0f; edge < hi && count < 256; edge += 1.0f, count++) cuts.push_back(edge);
    }
    std::sort(cuts.begin(), cuts.end());
    if (span < 0) std::reverse(cuts.begin(), cuts.end());

    for (size_t i = 0; i + 1 < cuts.size(); i++) {
        const float x0  = cuts[i];
        const float x1  = cuts[i + 1];
        const float mid = (x0 + x1) * 0.5f;
        float       ua  = x0;
        float       ub  = x1;
        if (wrap == 0) {
            if (mid < 0.0f) ua = ub = 0.0f;
            else if (mid > 1.0f) ua = ub = 1.0f;
        } else {
            const float tile = std::floor(mid);
            ua -= tile;
            ub -= tile;
            if (wrap == 2 && (static_cast<long>(std::abs(tile)) & 1)) {
                ua = 1.0f - ua;
                ub = 1.0f - ub;
            }
        }
        out.push_back({(x0 - u0) / span, (x1 - u0) / span, ua, ub});
    }
    return out;
}

void drawCanvas(App& app, Loaded* data, ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Canvas", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail  = ImGui::GetContentRegionAvail();
    ImDrawList*  dl     = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(24, 26, 32, 255));

    ImGui::InvisibleButton("##canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO&   io      = ImGui::GetIO();

    if (data && (app.needFit || (!app.userMoved && (avail.x != app.lastAvail.x || avail.y != app.lastAvail.y)))) {
        fitView(app, data->layout, avail);
        app.userMoved = false;
    }
    app.lastAvail = avail;

    const ImVec2 center(origin.x + avail.x * 0.5f, origin.y + avail.y * 0.5f);
    auto toScreen = [&](float x, float y) { return ImVec2(center.x + app.pan.x + x * app.zoom, center.y + app.pan.y + y * app.zoom); };

    if (hovered) {
        if (io.MouseWheel != 0.0f) {
            app.userMoved = true;
            const float before = app.zoom;
            app.zoom = std::clamp(app.zoom * std::pow(1.15f, io.MouseWheel), 0.02f, 80.0f);
            const ImVec2 m(io.MousePos.x - center.x - app.pan.x, io.MousePos.y - center.y - app.pan.y);
            app.pan.x -= m.x * (app.zoom / before - 1.0f);
            app.pan.y -= m.y * (app.zoom / before - 1.0f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
            app.userMoved = true;
            app.pan.x += io.MouseDelta.x;
            app.pan.y += io.MouseDelta.y;
        }
    }

    if (!data) {
        const char* msg = "Drop a layout archive, .blo or .brlyt here";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), IM_COL32(150, 155, 170, 255), msg);
        ImGui::End();
        return;
    }

    const Layout& layout = data->layout;

    if (app.showGrid) {
        float step = 100.0f;
        while (step * app.zoom < 18.0f) step *= 5.0f;
        const ImVec2 tl(origin.x, origin.y);
        const float x0 = std::floor(((tl.x - center.x - app.pan.x) / app.zoom) / step) * step;
        const float y0 = std::floor(((tl.y - center.y - app.pan.y) / app.zoom) / step) * step;
        for (float x = x0; toScreen(x, 0).x < origin.x + avail.x; x += step)
            dl->AddLine(ImVec2(toScreen(x, 0).x, origin.y), ImVec2(toScreen(x, 0).x, origin.y + avail.y), IM_COL32(255, 255, 255, 12));
        for (float y = y0; toScreen(0, y).y < origin.y + avail.y; y += step)
            dl->AddLine(ImVec2(origin.x, toScreen(0, y).y), ImVec2(origin.x + avail.x, toScreen(0, y).y), IM_COL32(255, 255, 255, 12));
    }

    dl->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), true);

    if (app.showFrame) {
        dl->AddRectFilled(toScreen(0, 0), toScreen(layout.width, layout.height), IM_COL32(12, 13, 18, 255));
        dl->AddRect(toScreen(0, 0), toScreen(layout.width, layout.height), IM_COL32(200, 205, 225, 120), 0.0f, 0, 1.5f);
    }

    const int count = static_cast<int>(layout.panes.size());
    std::vector<std::array<ImVec2, 4>> quads(count);
    for (int i = 0; i < count; i++) {
        for (int c = 0; c < 4; c++) quads[i][c] = toScreen(layout.panes[i].corners[c].x, layout.panes[i].corners[c].y);
    }

    app.hovered = -1;
    if (hovered) {
        for (int i = count - 1; i >= 0; i--) {
            if (!app.showHidden && !data->shown[i]) continue;
            if (inQuad(quads[i], io.MousePos)) {
                app.hovered = i;
                break;
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x == 0.0f) {
            app.selected = app.hovered;
            app.scrollTo = app.hovered;
        }
    }

    for (int i = 0; i < count; i++) {
        const Pane& p      = layout.panes[i];
        const bool  hidden = !data->shown[i];
        if (hidden && !app.showHidden) continue;

        const int alpha = hidden ? 70 : 255;
        const ImVec2* q = quads[i].data();

        SDL_Texture* tex = app.showTextures ? paneTexture(app, *data, p) : nullptr;

        if (tex) {
            const ImU32 tint = IM_COL32(255, 255, 255, hidden ? 70 : (app.useAlpha ? p.alpha_total : 255));
            auto lerp2 = [](ImVec2 a, ImVec2 b, float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); };
            auto pos   = [&](float a, float b) { return lerp2(lerp2(q[0], q[1], a), lerp2(q[3], q[2], a), b); };

            const bool axisAligned = std::abs(p.uv[1].y - p.uv[0].y) < 1e-4f && std::abs(p.uv[3].x - p.uv[0].x) < 1e-4f;
            if (axisAligned) {
                const std::vector<Segment> across = segments(p.uv[0].x, p.uv[1].x, p.wrap_s);
                const std::vector<Segment> down   = segments(p.uv[0].y, p.uv[3].y, p.wrap_t);
                for (const Segment& d : down) {
                    for (const Segment& a : across) {
                        dl->AddImageQuad(reinterpret_cast<ImTextureID>(tex), pos(a.t0, d.t0), pos(a.t1, d.t0), pos(a.t1, d.t1), pos(a.t0, d.t1),
                                         ImVec2(a.ua, d.ua), ImVec2(a.ub, d.ua), ImVec2(a.ub, d.ub), ImVec2(a.ua, d.ub), tint);
                    }
                }
            } else {
                dl->AddImageQuad(reinterpret_cast<ImTextureID>(tex), q[0], q[1], q[2], q[3], ImVec2(p.uv[0].x, p.uv[0].y), ImVec2(p.uv[1].x, p.uv[1].y),
                                 ImVec2(p.uv[2].x, p.uv[2].y), ImVec2(p.uv[3].x, p.uv[3].y), tint);
            }
        } else if (app.showFill && !hidden) {
            const uint32_t c = p.color;
            const int ca = static_cast<int>((c & 0xFF) * ((app.useAlpha ? p.alpha_total : 255) / 255.0f) * 0.45f);
            ImU32 fill = (p.kind == "PIC2" || p.kind == "pic1") ? IM_COL32((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, std::max(ca, 28))
                                                                 : kindColor(p.kind, 22);
            dl->AddConvexPolyFilled(q, 4, fill);
        }

        if (app.showText && !p.text.empty() && !hidden) {
            const float size = p.font_size.y * app.zoom * std::abs(p.scale.y);
            if (size >= 5.0f) {
                const ImVec2 mid((q[0].x + q[2].x) * 0.5f, (q[0].y + q[2].y) * 0.5f);
                const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, p.text.c_str());
                const uint32_t c = p.color;
                dl->AddText(ImGui::GetFont(), size, ImVec2(mid.x - ts.x * 0.5f, mid.y - ts.y * 0.5f), IM_COL32((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, 255),
                            p.text.c_str());
            }
        }

        const bool sel = i == app.selected;
        const bool hov = i == app.hovered;
        dl->AddPolyline(q, 4, sel ? IM_COL32(255, 220, 90, 255) : (hov ? IM_COL32(255, 255, 255, 230) : kindColor(p.kind, alpha * 0.7f)),
                        ImDrawFlags_Closed, sel ? 2.5f : (hov ? 2.0f : 1.0f));

        if (app.showLabels && !p.name.empty()) {
            const float w = std::abs(q[1].x - q[0].x) + std::abs(q[1].y - q[0].y);
            if (w > 40.0f) {
                ImVec2 tl(std::min({q[0].x, q[1].x, q[2].x, q[3].x}), std::min({q[0].y, q[1].y, q[2].y, q[3].y}));
                dl->AddText(ImVec2(tl.x + 3, tl.y + 2), IM_COL32(230, 232, 245, hidden ? 90 : 190), p.name.c_str());
            }
        }
    }

    if (app.selected >= 0 && app.selected < count) {
        const ImVec2* q = quads[app.selected].data();
        const ImVec2 mid((q[0].x + q[2].x) * 0.5f, (q[0].y + q[2].y) * 0.5f);
        dl->AddCircleFilled(mid, 3.5f, IM_COL32(255, 220, 90, 255));
        dl->AddLine(ImVec2(mid.x - 8, mid.y), ImVec2(mid.x + 8, mid.y), IM_COL32(255, 220, 90, 200));
        dl->AddLine(ImVec2(mid.x, mid.y - 8), ImVec2(mid.x, mid.y + 8), IM_COL32(255, 220, 90, 200));
    }
    dl->PopClipRect();

    if (app.hovered >= 0) {
        const Pane& p = layout.panes[app.hovered];
        ImGui::BeginTooltip();
        ImGui::TextColored(ImColor(kindColor(p.kind, 255)), "%s", kindLabel(p.kind));
        ImGui::SameLine();
        ImGui::TextUnformatted(p.name.empty() ? "(unnamed)" : p.name.c_str());
        ImGui::TextDisabled("%.1f x %.1f", p.size.x, p.size.y);
        ImGui::EndTooltip();
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10, origin.y + 8));
    ImGui::TextDisabled("%.0f%%   wheel: zoom   drag middle/right: pan   F: fit", app.zoom * 100.0f);
    ImGui::End();
}

void drawMenu(App& app, bool& quit) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open path...")) app.openPopup = true;
            if (ImGui::MenuItem("Quit")) quit = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Fit to window", "F")) app.needFit = true;
            ImGui::Separator();
            ImGui::MenuItem("Grid", nullptr, &app.showGrid);
            ImGui::MenuItem("Layout frame", nullptr, &app.showFrame);
            ImGui::MenuItem("Textures", nullptr, &app.showTextures);
            ImGui::MenuItem("Text", nullptr, &app.showText);
            ImGui::MenuItem("Apply pane alpha", nullptr, &app.useAlpha);
            ImGui::MenuItem("Fill colors", nullptr, &app.showFill);
            ImGui::MenuItem("Pane labels", nullptr, &app.showLabels);
            ImGui::MenuItem("Show hidden panes", nullptr, &app.showHidden);
            ImGui::EndMenu();
        }
        if (!app.source.empty()) {
            ImGui::TextDisabled("   %s", app.source.c_str());
        }
        ImGui::EndMainMenuBar();
    }

    if (app.openPopup) {
        ImGui::OpenPopup("Open");
        app.openPopup = false;
    }
    if (ImGui::BeginPopupModal("Open", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(520.0f);
        const bool enter = ImGui::InputTextWithHint("##p", "Path to .arc / .blo / .brlyt / folder", app.openPath, sizeof(app.openPath),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::Button("Open")) {
            loadSource(app, app.openPath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void applyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 6.0f;
    s.FrameRounding     = 4.0f;
    s.ChildRounding     = 4.0f;
    s.ScrollbarRounding = 6.0f;
    s.FramePadding      = ImVec2(8, 5);
    s.ItemSpacing       = ImVec2(8, 6);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]      = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    c[ImGuiCol_ChildBg]       = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
    c[ImGuiCol_TitleBg]       = ImVec4(0.12f, 0.13f, 0.17f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.17f, 0.23f, 1.0f);
    c[ImGuiCol_Header]        = ImVec4(0.24f, 0.36f, 0.60f, 0.55f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.44f, 0.72f, 0.65f);
    c[ImGuiCol_HeaderActive]  = ImVec4(0.30f, 0.44f, 0.72f, 0.85f);
    c[ImGuiCol_FrameBg]       = ImVec4(0.16f, 0.17f, 0.22f, 1.0f);
    c[ImGuiCol_Button]        = ImVec4(0.22f, 0.32f, 0.54f, 1.0f);
}

} // namespace

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window*   window   = SDL_CreateWindow("Reservoir Layout Viewer", 1440, 860, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!window || !renderer) {
        std::fprintf(stderr, "window creation failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    applyTheme();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    App app;
    app.renderer = renderer;
    if (argc > 1) loadSource(app, argv[1]);
    if (argc > 2) {
        for (int i = 0; i < static_cast<int>(app.entries.size()); i++) {
            if (contains(app.entries[i].name, argv[2])) {
                app.entry = i;
                break;
            }
        }
    }

    bool quit = false;
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) quit = true;
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) loadSource(app, event.drop.data);
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (ImGui::IsKeyPressed(ImGuiKey_F) && !ImGui::GetIO().WantTextInput) app.needFit = true;

        Loaded* data = current(app);

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float  top     = ImGui::GetFrameHeight();
        const float  leftW   = 330.0f;
        const float  rightW  = 320.0f;
        const float  h       = display.y - top;

        drawMenu(app, quit);
        drawLeftPanel(app, data, ImVec2(0, top), ImVec2(leftW, h));
        drawInspector(app, data, ImVec2(display.x - rightW, top), ImVec2(rightW, h));
        drawCanvas(app, data, ImVec2(leftW, top), ImVec2(display.x - leftW - rightW, h));

        if (!app.error.empty()) {
            ImGui::SetNextWindowPos(ImVec2(leftW + 16, display.y - 64));
            ImGui::Begin("##err", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", app.error.c_str());
            ImGui::End();
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 22, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    app.cache.clear();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
