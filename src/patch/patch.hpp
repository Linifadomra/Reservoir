#pragma once

#ifndef RESERVOIR_PATCH_H
#define RESERVOIR_PATCH_H

#include "blo/blo.hpp"
#include <string>
#include <vector>

namespace Reservoir {

enum class PatchAction { Add, Delete, Edit };
enum class FieldType { Float, String, Int, U8, U16 };

struct FieldEdit {
    std::string name;
    FieldType   type;
    float       float_val  = 0.f;
    std::string string_val;
    int64_t     int_val    = 0;
};

struct PatchEntry {
    PatchAction action;

    std::string parent;
    std::string element_name;
    std::string type;

    std::string secondary_name;
    int         bck_idx      = 0;
    int         enabled      = 1;
    std::string anchor;
    float       size_x       = 0.f, size_y  = 0.f;
    float       scale_x      = 1.f, scale_y = 1.f;
    float       rotation     = 0.f;
    float       offset_x     = 0.f, offset_y = 0.f;
    std::string has_children;

    int         unk_idx      = 0;
    int         mat_idx      = 0;
    std::string mat_name;

    std::vector<int> unk_indexes;
    std::vector<int> uv_coords;
    std::vector<int> colors;

    int         char_space   = 0;
    int         line_space   = 0;
    int         font_size_x  = 0;
    int         font_size_y  = 0;
    std::string h_bind;
    std::string v_bind;
    std::vector<int> char_color;
    std::vector<int> grad_color;
    int         connected    = 0;
    int         text_cutoff  = 0;
    std::string text;

    std::vector<FieldEdit> fields_to_edit;
};

struct PatchHeader {
    std::string blo_file;
    std::string new_filename;
    std::string arc;
    std::string action;
};

struct PatchDocument {
    PatchHeader            header;
    std::vector<PatchEntry> entries;
};

void apply_patch(BLO& blo, const PatchDocument& patch);

PatchDocument parse_patch_document(const std::string& json_text);

}

#endif