#include "patch.hpp"
#include "blo/blo.hpp"
#include "json.hpp"
#include <cstdint>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>

using json = nlohmann::json;

namespace Reservoir {

static PAN2Node& as_pan2(ElementNode& n) { return std::get<PAN2Node>(n.node); }
static PIC2Node& as_pic2(ElementNode& n) { return std::get<PIC2Node>(n.node); }
static TBX2Node& as_tbx2(ElementNode& n) { return std::get<TBX2Node>(n.node); }
static const PAN2Node& as_pan2(const ElementNode& n) { return std::get<PAN2Node>(n.node); }
static const PIC2Node& as_pic2(const ElementNode& n) { return std::get<PIC2Node>(n.node); }
static const TBX2Node& as_tbx2(const ElementNode& n) { return std::get<TBX2Node>(n.node); }

static PAN2Node& base_pan(ElementNode& n) {
    return std::visit([](auto& v) -> PAN2Node& {
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, PAN2Node>)
            return v;
        else
            return v.base;
    }, n.node);
}

static std::string strip_prefix(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (uint8_t)s[start] < 32) ++start;
    size_t end = s.find_last_not_of('\x00');
    if (end == std::string::npos || end < start) return "";
    return s.substr(start, end - start + 1);
}

static const PAN2Node& base_pan(const ElementNode& n) {
    return std::visit([](const auto& v) -> const PAN2Node& {
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, PAN2Node>)
            return v;
        else
            return v.base;
    }, n.node);
}

static std::vector<ElementNode>& children_of(ElementNode& n) {
    return as_pan2(n).children;
}

static uint8_t get_anchor(const std::string& type)
{
    if (type == "Top-Left")      return 0;
    if (type == "Center-Top")    return 1;
    if (type == "Top-Right")     return 2;
    if (type == "Center-Left")   return 3;
    if (type == "Center")        return 4;
    if (type == "Center-Right")  return 5;
    if (type == "Bottom-Left")   return 6;
    if (type == "Center-Bottom") return 7;
    if (type == "Bottom-Right")  return 8;
    throw std::invalid_argument("unknown anchor: " + type);
}

static uint8_t get_horiz_bind(const std::string& type)
{
    if (type == "Center") return 0;
    if (type == "Right")  return 1;
    if (type == "Left")   return 2;
    throw std::invalid_argument("unknown h_bind: " + type);
}

static uint8_t get_vert_bind(const std::string& type)
{
    if (type == "Center") return 0;
    if (type == "Bottom") return 1;
    if (type == "Top")    return 2;
    throw std::invalid_argument("unknown v_bind: " + type);
}

static void pad_to_alignment(std::vector<uint8_t>& v, size_t align)
{
    size_t i = 0;
    while (v.size() % align != 0) {
        v.push_back(PADDING_BYTES[i % PADDING_LENGTH]);
        ++i;
    }
}

static ElementNode* depth_first_search(ElementNode* node, const std::string& target)
{
    if (node->type != ElementNode::Type::PAN2)
        return nullptr;

    if (strip_prefix(base_pan(*node).info_tag) == target) {
        return node;
    }

    for (auto& child : as_pan2(*node).children) {
        ElementNode* result = depth_first_search(&child, target);
        if (result) return result;
    }
    return nullptr;
}

static void apply_field_edit(ElementNode& node, const FieldEdit& fe)
{
    auto set_pan2_field = [&](PAN2Node& p) -> bool {
        if (fe.name == "size_x")      { p.size_x      = fe.float_val; return true; }
        if (fe.name == "size_y")      { p.size_y      = fe.float_val; return true; }
        if (fe.name == "scale_x")     { p.scale_x     = fe.float_val; return true; }
        if (fe.name == "scale_y")     { p.scale_y     = fe.float_val; return true; }
        if (fe.name == "rotate_x")    { p.rotate_x    = fe.float_val; return true; }
        if (fe.name == "rotate_y")    { p.rotate_y    = fe.float_val; return true; }
        if (fe.name == "rotate_z")    { p.rotate_z    = fe.float_val; return true; }
        if (fe.name == "translate_x") { p.translate_x = fe.float_val; return true; }
        if (fe.name == "translate_y") { p.translate_y = fe.float_val; return true; }
        if (fe.name == "visible")     { p.visible     = (uint8_t)fe.int_val;  return true; }
        if (fe.name == "base_position"){ p.base_position = (uint8_t)fe.int_val; return true; }
        if (fe.name == "bck_idx")     { p.bck_idx     = (uint16_t)fe.int_val; return true; }
        return false;
    };

    if (node.type == ElementNode::Type::PAN2) {
        set_pan2_field(as_pan2(node));
        return;
    }

    bool handled = false;
    if (node.type == ElementNode::Type::PIC2) {
        PIC2Node& p = as_pic2(node);
        if (fe.name == "field_0x0")    { p.field_0x0    = (uint16_t)fe.int_val; handled = true; }
        if (fe.name == "field_0x2")    { p.field_0x2    = (uint16_t)fe.int_val; handled = true; }
        if (fe.name == "material_num") { p.material_num = (uint16_t)fe.int_val; handled = true; }
    } else if (node.type == ElementNode::Type::TBX2) {
        TBX2Node& t = as_tbx2(node);
        if (fe.name == "char_space")  { t.char_space  = (uint16_t)fe.int_val; handled = true; }
        if (fe.name == "line_space")  { t.line_space  = (uint16_t)fe.int_val; handled = true; }
        if (fe.name == "font_size_x") { t.font_size_x = (uint16_t)fe.int_val; handled = true; }
        if (fe.name == "font_size_y") { t.font_size_y = (uint16_t)fe.int_val; handled = true; }
        if (fe.name == "text") {
            auto encoded = std::vector<uint8_t>(fe.string_val.begin(), fe.string_val.end());
            t.field_0x1e  = (uint16_t)encoded.size();
            t.end_padding = encoded;
            pad_to_alignment(t.end_padding, 8);
            handled = true;
        }
    }
    if (!handled) {
        if (node.type == ElementNode::Type::PIC2)
            set_pan2_field(as_pic2(node).base);
        else if (node.type == ElementNode::Type::TBX2)
            set_pan2_field(as_tbx2(node).base);
        else if (node.type == ElementNode::Type::WIN2)
            set_pan2_field(std::get<WIN2Node>(node.node).base);
    }
}

static PAN2Node make_pan2_base(const PatchEntry& e)
{
    PAN2Node p{};
    p.bck_idx       = (uint16_t)e.bck_idx;
    p.visible       = (uint8_t)e.enabled;
    p.base_position = get_anchor(e.anchor);
    p.info_tag      = e.element_name;
    p.user_info_tag = e.secondary_name;
    p.size_x        = e.size_x;
    p.size_y        = e.size_y;
    p.scale_x       = e.scale_x;
    p.scale_y       = e.scale_y;
    p.rotate_x      = e.rotation;
    p.rotate_y      = e.rotation;
    p.rotate_z      = e.rotation;
    p.translate_x   = e.offset_x;
    p.translate_y   = e.offset_y;
    p.field_0x8  = 64;
    p.padding[0] = 0x52;
    p.padding[1] = 0x45;
    return p;
}

static ElementNode make_pan2_node(const PatchEntry& e)
{
    ElementNode n;
    n.type = ElementNode::Type::PAN2;
    PAN2Node p = make_pan2_base(e);
    if (e.has_children == "Yes") {
        n.has_children_bgn1_tag  = true;
        n.children_bgn1_tag_size = 8;
        n.has_end_tag            = true;
        n.end_tag_size           = 8;
    }
    n.node = std::move(p);
    return n;
}

static ElementNode make_pic2_node(const PatchEntry& e)
{
    ElementNode n;
    n.type = ElementNode::Type::PIC2;
    PIC2Node p{};
    p.base              = make_pan2_base(e);
    p.pan2_sub_tag_size = 72;          // embedded 'pan2' sub-header, size=72
    p.field_0x0         = 48;
    p.field_0x2         = (uint16_t)e.unk_idx;
    p.material_num      = (uint16_t)e.mat_idx;
    p.field_0x6         = 0x5245;      // 'RE' — matches Python new_pic2
    for (size_t i = 0; i < 4 && i < e.unk_indexes.size(); ++i)
        p.field_0x8[i] = (uint16_t)e.unk_indexes[i];
    for (size_t i = 0; i < 8 && i < e.uv_coords.size(); ++i)
        p.field_0x10[i] = (uint16_t)e.uv_coords[i];
    for (size_t c = 0; c < 4 && (c+1)*4 <= e.colors.size(); ++c) {
        p.corner_color[c] =
            ((uint32_t)(uint8_t)e.colors[c*4+0] << 24) |
            ((uint32_t)(uint8_t)e.colors[c*4+1] << 16) |
            ((uint32_t)(uint8_t)e.colors[c*4+2] <<  8) |
             (uint32_t)(uint8_t)e.colors[c*4+3];
    }
    n.node = std::move(p);
    return n;
}

static ElementNode make_tbx2_node(const PatchEntry& e)
{
    ElementNode n;
    n.type = ElementNode::Type::TBX2;
    TBX2Node t{};
    t.base              = make_pan2_base(e);
    t.pan2_sub_tag_size = 72;          // embedded 'pan2' sub-header, size=72
    t.field_0x2         = (uint16_t)e.unk_idx;
    t.material_num      = 0;
    t.char_space        = (uint16_t)e.char_space;
    t.line_space        = (uint16_t)e.line_space;
    t.font_size_x       = (uint16_t)e.font_size_x;
    t.font_size_y       = (uint16_t)e.font_size_y;
    t.h_bind            = get_horiz_bind(e.h_bind);
    t.v_bind            = get_vert_bind(e.v_bind);
    for (size_t i = 0; i < 4 && i < e.char_color.size(); ++i)
        t.char_color[i] = (uint8_t)e.char_color[i];
    for (size_t i = 0; i < 4 && i < e.grad_color.size(); ++i)
        t.grad_color[i] = (uint8_t)e.grad_color[i];
    t.connected  = (uint8_t)e.connected;
    t.field_0x1c = (uint16_t)e.text_cutoff;
    auto text_bytes = std::vector<uint8_t>(e.text.begin(), e.text.end());
    t.field_0x1e = (uint16_t)text_bytes.size();
    t.end_padding = text_bytes;
    pad_to_alignment(t.end_padding, 8);
    n.node = std::move(t);
    return n;
}

static void set_element_mat_no(ElementNode& node, const MAT1Section& mat1)
{
    if (node.type == ElementNode::Type::PAN2) {
        for (auto& child : as_pan2(node).children)
            set_element_mat_no(child, mat1);
        return;
    }

    const std::string& target = (node.type == ElementNode::Type::PAN2)
    ? as_pan2(node).info_tag
    : (node.type == ElementNode::Type::PIC2)
        ? as_pic2(node).base.info_tag
        : (node.type == ElementNode::Type::TBX2)
            ? as_tbx2(node).base.info_tag
            : std::get<WIN2Node>(node.node).base.info_tag;
    for (size_t i = 0; i < mat1.mat_name_table.mat_names.size(); ++i) {
        if (mat1.mat_name_table.mat_names[i].find(target) != std::string::npos) {
            if (node.type == ElementNode::Type::PIC2)
                as_pic2(node).material_num = (uint16_t)i;
            else if (node.type == ElementNode::Type::TBX2)
                as_tbx2(node).material_num = (uint16_t)i;
            break;
        }
    }
}

static void set_new_mat_no(ElementNode& node,
                            const std::vector<PatchEntry>& entries,
                            const MAT1Section& mat1)
{
    if (node.type == ElementNode::Type::PAN2) {
        for (auto& child : as_pan2(node).children)
            set_new_mat_no(child, entries, mat1);
        return;
    }
    if (node.type != ElementNode::Type::PIC2 && node.type != ElementNode::Type::TBX2)
        return;

    const std::string& target_elem = (node.type == ElementNode::Type::PAN2)
    ? as_pan2(node).info_tag
    : (node.type == ElementNode::Type::PIC2)
        ? as_pic2(node).base.info_tag
        : (node.type == ElementNode::Type::TBX2)
            ? as_tbx2(node).base.info_tag
            : std::get<WIN2Node>(node.node).base.info_tag;
    for (const auto& entry : entries) {
        if (entry.mat_name.empty()) continue;
        if (entry.element_name.find(target_elem) == std::string::npos) continue;
        if (entry.action != PatchAction::Add && entry.action != PatchAction::Edit) continue;
        for (size_t i = 0; i < mat1.mat_name_table.mat_names.size(); ++i) {
            if (mat1.mat_name_table.mat_names[i].find(entry.mat_name) != std::string::npos) {
                if (node.type == ElementNode::Type::PIC2)
                    as_pic2(node).material_num = (uint16_t)i;
                else
                    as_tbx2(node).material_num = (uint16_t)i;
                break;
            }
        }
    }
}

static int calc_blocks(const ElementNode& node)
{
    if (node.type != ElementNode::Type::PAN2)
        return 2;

    const auto& ch = as_pan2(node).children; 
    int count = ch.empty() ? 2 : 2;
    for (const auto& child : ch)
        count += calc_blocks(child);
    return count;
}

static void collect_pan2_parents_with_pic2(ElementNode& node,
                                            std::vector<ElementNode*>& out)
{
    if (node.type != ElementNode::Type::PAN2) return;

    auto& ch = as_pan2(node).children;
    bool has_pic2 = false;
    for (auto& child : ch) {
        if (child.type == ElementNode::Type::PIC2) { has_pic2 = true; break; }
        if (child.type == ElementNode::Type::PAN2) {
            for (auto& gc : as_pan2(child).children) {
                if (gc.type == ElementNode::Type::PIC2) { has_pic2 = true; break; }
            }
        }
        if (has_pic2) break;
    }
    if (has_pic2) out.push_back(&node);

    for (auto& child : ch)
        collect_pan2_parents_with_pic2(child, out);
}

static bool is_descendant_of(const ElementNode* node,
                               const ElementNode* potential_parent)
{
    if (potential_parent->type != ElementNode::Type::PAN2) return false;
    for (const auto& child : std::get<PAN2Node>(potential_parent->node).children) {
        if (&child == node) return true;
        if (is_descendant_of(node, &child)) return true;
    }
    return false;
}

static std::vector<ElementNode*>
remove_nested_nodes(std::vector<ElementNode*>& nodes)
{
    std::vector<ElementNode*> filtered;
    for (auto* node : nodes) {
        const std::string tag = strip_prefix(base_pan(*node).info_tag);
        if (tag == "ROOT" || tag == "n_all") continue;

        bool is_desc = false;
        for (auto* other : nodes) {
            if (other == node) continue;
            const std::string otag = strip_prefix(base_pan(*other).info_tag);
            if (otag == "ROOT" || otag == "n_all") continue;
            if (is_descendant_of(node, other)) { is_desc = true; break; }
        }
        if (!is_desc) filtered.push_back(node);
    }
    return filtered;
}

static void get_pic2s(ElementNode& node,
                       std::vector<ElementNode*>& pic2_list)
{
    auto& ch = as_pan2(node).children;

    std::vector<ElementNode*> per_pan;
    for (auto& child : ch)
        if (child.type == ElementNode::Type::PIC2)
            per_pan.push_back(&child);

    std::sort(per_pan.begin(), per_pan.end(), [](ElementNode* a, ElementNode* b){
        return strip_prefix(base_pan(*a).info_tag) < strip_prefix(base_pan(*b).info_tag);
    });

    for (auto* pic : per_pan) {
        bool dup = false;
        for (auto* existing : pic2_list)
            if (base_pan(*existing).info_tag == base_pan(*pic).info_tag) { dup = true; break; }
        if (!dup) pic2_list.push_back(pic);
    }

    std::vector<ElementNode*> pan2_children;
    for (auto& child : ch)
        if (child.type == ElementNode::Type::PAN2)
            pan2_children.push_back(&child);

    std::sort(pan2_children.begin(), pan2_children.end(), [](ElementNode* a, ElementNode* b){
        return strip_prefix(base_pan(*a).info_tag) < strip_prefix(base_pan(*b).info_tag);
    });

    for (auto* pan : pan2_children)
        get_pic2s(*pan, pic2_list);
}

static void reassign_unk_indexes(ElementNode& root,
                                  std::vector<ElementNode*>& pic2_list)
{
    int cur_pic2_idx = 0;
    int unk_idx      = 0;

    std::function<bool(ElementNode&)> iterate = [&](ElementNode& node) -> bool {
        if (cur_pic2_idx >= (int)pic2_list.size()) return true;

        if (node.type == ElementNode::Type::PAN2) {
            for (auto& child : as_pan2(node).children)
                if (iterate(child)) return true;
        } else if (node.type == ElementNode::Type::PIC2) {
            if (base_pan(node).info_tag == base_pan(*pic2_list[cur_pic2_idx]).info_tag) {
                for (int i = 0; i < 4; ++i)
                    as_pic2(node).field_0x8[i] = (uint16_t)unk_idx++;
                ++cur_pic2_idx;
                return true;
            }
        }
        return false;
    };

    while (cur_pic2_idx < (int)pic2_list.size())
        iterate(root);
}

static uint32_t calc_elements_size(const ElementNode& node)
{
    if (node.type == ElementNode::Type::PAN2) {
        const auto& p = std::get<PAN2Node>(node.node);
        uint32_t sz = 72 + node.leading_bgn1_tag_size + node.children_bgn1_tag_size + node.end_tag_size;
        for (const auto& child : p.children)
            sz += calc_elements_size(child);
        return sz;
    }
    if (node.type == ElementNode::Type::PIC2) {
        const auto& pic = std::get<PIC2Node>(node.node);
        uint32_t sub = pic.pan2_sub_tag_size > 0 ? 8u : 0u;
        return 8 + sub + 64 + 48 + (uint32_t)pic.end_padding.size();
    }
    if (node.type == ElementNode::Type::TBX2) {
        const auto& t = std::get<TBX2Node>(node.node);
        uint32_t sub = t.pan2_sub_tag_size > 0 ? 8u : 0u;
        return 8 + sub + 64 + 40 + (uint32_t)t.end_padding.size();
    }
    return 0;
}

void apply_patch(BLO& blo, const PatchDocument& patch)
{
    ElementNode& root = blo.root;
    std::function<void(const ElementNode&)> find_let05 = [&](const ElementNode& n) {
        if (strip_prefix(base_pan(n).info_tag) == "let_05_n") {
            if (n.type == ElementNode::Type::PAN2) {
                for (const auto& child : std::get<PAN2Node>(n.node).children) {
                    std::cerr << "  child info_tag=[" << base_pan(child).info_tag.size() << "] '";
                    for (char c : base_pan(child).info_tag)
                        std::cerr << (c >= 32 && c < 127 ? c : '?');
                    std::cerr << "'\n";
                }
            }
        }
        if (n.type == ElementNode::Type::PAN2)
            for (const auto& child : std::get<PAN2Node>(n.node).children)
                find_let05(child);
    };
    find_let05(root);

    for (const auto& entry : patch.entries) {
        ElementNode* parent_node = depth_first_search(&root, entry.parent);
        if (!parent_node) {
            throw std::runtime_error("patch: parent '" + entry.parent + "' not found");
        }

        if (parent_node->type != ElementNode::Type::PAN2) {
            throw std::runtime_error("patch: parent '" + entry.parent + "' is not a PAN2 node (type=" + std::to_string((int)parent_node->type) + ")");
        }

        switch (entry.action) {
            case PatchAction::Add: {
                auto& ch = as_pan2(*parent_node).children;
                if (entry.type == "PAN2") {
                    ch.push_back(make_pan2_node(entry));
                } else if (entry.type == "PIC2") {
                    ch.push_back(make_pic2_node(entry));
                } else if (entry.type == "TBX2") {
                    ch.push_back(make_tbx2_node(entry));
                }
                break;
            }
            case PatchAction::Delete: {
                auto& children = as_pan2(*parent_node).children;
                children.erase(
                    std::remove_if(children.begin(), children.end(),
                        [&](const ElementNode& child) {
                            const std::string& name = (child.type == ElementNode::Type::PAN2)
                                ? as_pan2(child).info_tag
                                : (child.type == ElementNode::Type::PIC2)
                                    ? as_pic2(child).base.info_tag
                                    : (child.type == ElementNode::Type::TBX2)
                                        ? as_tbx2(child).base.info_tag
                                        : std::get<WIN2Node>(child.node).base.info_tag;
                            return strip_prefix(name) == entry.element_name;
                        }),
                    children.end());
                break;
            }
            case PatchAction::Edit: {
                for (auto& child : as_pan2(*parent_node).children) {
                    const std::string& name = (child.type == ElementNode::Type::PAN2)
                        ? as_pan2(child).info_tag
                        : (child.type == ElementNode::Type::PIC2)
                            ? as_pic2(child).base.info_tag
                            : (child.type == ElementNode::Type::TBX2)
                                ? as_tbx2(child).base.info_tag
                                : std::get<WIN2Node>(child.node).base.info_tag;
                    if (strip_prefix(name) == entry.element_name) {
                        for (const auto& fe : entry.fields_to_edit)
                            apply_field_edit(child, fe);
                    }
                }
                break;
            }
        }
    }

    set_element_mat_no(root, blo.mat1);
    set_new_mat_no(root, patch.entries, blo.mat1);
    blo.blocks = 4 + calc_blocks(root);

    std::vector<ElementNode*> all_pan2;
    collect_pan2_parents_with_pic2(root, all_pan2);

    std::sort(all_pan2.begin(), all_pan2.end(), [](ElementNode* a, ElementNode* b){
        return strip_prefix(base_pan(*a).info_tag) < strip_prefix(base_pan(*b).info_tag);
    });

    std::vector<ElementNode*> node_list = remove_nested_nodes(all_pan2);

    ElementNode* n_all_node = nullptr;
    for (auto* n : all_pan2) {
        const std::string& tag = (n->type == ElementNode::Type::PAN2)
            ? std::get<PAN2Node>(n->node).info_tag
            : (n->type == ElementNode::Type::PIC2)
                ? std::get<PIC2Node>(n->node).base.info_tag
                : (n->type == ElementNode::Type::TBX2)
                    ? std::get<TBX2Node>(n->node).base.info_tag
                    : std::get<WIN2Node>(n->node).base.info_tag;
        if (strip_prefix(tag) == "n_all") { n_all_node = n; break; }
    }

    if (n_all_node) {
        bool has_direct_pic2 = false;
        for (const auto& child : std::get<PAN2Node>(n_all_node->node).children)
            if (child.type == ElementNode::Type::PIC2) { has_direct_pic2 = true; break; }

        if (has_direct_pic2) {
            const std::string insert_name = "n_all";
            size_t idx = node_list.size();
            for (size_t i = 0; i < node_list.size(); ++i) {
                const std::string& tag = (node_list[i]->type == ElementNode::Type::PAN2)
                    ? std::get<PAN2Node>(node_list[i]->node).info_tag
                    : (node_list[i]->type == ElementNode::Type::PIC2)
                        ? std::get<PIC2Node>(node_list[i]->node).base.info_tag
                        : (node_list[i]->type == ElementNode::Type::TBX2)
                            ? std::get<TBX2Node>(node_list[i]->node).base.info_tag
                            : std::get<WIN2Node>(node_list[i]->node).base.info_tag;
                if (strip_prefix(tag) > insert_name) { idx = i; break; }
            }
            node_list.insert(node_list.begin() + idx, n_all_node);
        }
    }

    std::vector<ElementNode*> pic2_list;
    for (auto* node : node_list)
        get_pic2s(*node, pic2_list);

    reassign_unk_indexes(root, pic2_list);
}

static FieldType parse_field_type(const std::string& s)
{
    if (s == "float")  return FieldType::Float;
    if (s == "string") return FieldType::String;
    if (s == "int")    return FieldType::Int;
    if (s == "u8")     return FieldType::U8;
    if (s == "u16")    return FieldType::U16;
    throw std::invalid_argument("unknown field type: " + s);
}

static PatchAction parse_action(const std::string& s)
{
    if (s == "add")    return PatchAction::Add;
    if (s == "delete") return PatchAction::Delete;
    if (s == "edit")   return PatchAction::Edit;
    throw std::invalid_argument("unknown action: " + s);
}

PatchDocument parse_patch_document(const std::string& json_text)
{
    auto root = json::parse(json_text);

    PatchDocument doc;

    const auto& hdr = root[0];
    doc.header.blo_file     = hdr.value("blo_file",     "");
    doc.header.new_filename = hdr.value("new_filename",  "");
    doc.header.arc          = hdr.value("arc",           "");
    doc.header.action       = hdr.value("action",        "");

    const auto& entries_json = root[1];
    for (const auto& ej : entries_json) {
        PatchEntry pe;
        pe.action       = parse_action(ej.value("action", "edit"));
        pe.parent       = ej.value("parent",       "");
        pe.element_name = ej.value("element_name", "");
        pe.type         = ej.value("type",         "");
        pe.mat_name     = ej.value("mat_name",     "");

        pe.secondary_name = ej.value("secondary_name", "");
        pe.bck_idx        = ej.value("bck_idx",   0);
        pe.enabled        = ej.value("enabled",    1);
        pe.anchor         = ej.value("anchor",     "Center");
        pe.size_x         = ej.value("size_x",     0.f);
        pe.size_y         = ej.value("size_y",     0.f);
        pe.scale_x        = ej.value("scale_x",    1.f);
        pe.scale_y        = ej.value("scale_y",    1.f);
        pe.rotation       = ej.value("rotation",   0.f);
        pe.offset_x       = ej.value("offset_x",   0.f);
        pe.offset_y       = ej.value("offset_y",   0.f);
        pe.has_children   = ej.value("has_children","No");

        pe.unk_idx  = ej.value("unk_idx",  0);
        pe.mat_idx  = ej.value("mat_idx",  0);
        if (ej.contains("unk_indexes"))
            for (auto v : ej["unk_indexes"]) pe.unk_indexes.push_back(v.get<int>());
        if (ej.contains("uv_coords"))
            for (auto v : ej["uv_coords"])   pe.uv_coords.push_back(v.get<int>());
        if (ej.contains("colors"))
            for (auto v : ej["colors"])      pe.colors.push_back(v.get<int>());

        pe.char_space   = ej.value("char_space",  0);
        pe.line_space   = ej.value("line_space",  0);
        pe.font_size_x  = ej.value("font_size_x", 0);
        pe.font_size_y  = ej.value("font_size_y", 0);
        pe.h_bind       = ej.value("h_bind",      "Center");
        pe.v_bind       = ej.value("v_bind",      "Center");
        if (ej.contains("char_color"))
            for (auto v : ej["char_color"]) pe.char_color.push_back(v.get<int>());
        if (ej.contains("grad_color"))
            for (auto v : ej["grad_color"]) pe.grad_color.push_back(v.get<int>());
        pe.connected    = ej.value("connected",   0);
        pe.text_cutoff  = ej.value("text_cutoff", 0);
        pe.text         = ej.value("text",        "");

        if (ej.contains("fields_to_edit")) {
            for (const auto& fname : ej["fields_to_edit"]) {
                std::string field = fname.get<std::string>();
                FieldEdit fe;
                fe.name = field;
                fe.type = parse_field_type(ej.value(field + "-type", "float"));
                switch (fe.type) {
                    case FieldType::Float:
                        fe.float_val  = ej.value(field + "-new_value", 0.f);
                        break;
                    case FieldType::String:
                        fe.string_val = ej.value(field + "-new_value", std::string{});
                        break;
                    case FieldType::Int:
                    case FieldType::U8:
                    case FieldType::U16:
                        fe.int_val    = ej.value(field + "-new_value", (int64_t)0);
                        break;
                }
                pe.fields_to_edit.push_back(std::move(fe));
            }
        }

        doc.entries.push_back(std::move(pe));
    }

    return doc;
}

} // namespace Reservoir