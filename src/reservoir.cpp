#include "reservoir/reservoir.hpp"
#include "blo/blo.hpp"
#include "patch/patch.hpp"

#include <confluence/rarc.h>
#include <confluence/yaz0.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace Reservoir {

BLO              parse_blo(const std::vector<uint8_t>& data);
std::vector<uint8_t> serialize_blo(const BLO& blo);

static std::vector<uint8_t> read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open: " + p.string());
    return { std::istreambuf_iterator<char>(f), {} };
}

static void write_file(const fs::path& p, const std::vector<uint8_t>& data)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write: " + p.string());
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

static std::string read_text(const fs::path& p)
{
    std::ifstream f(p);
    if (!f) throw std::runtime_error("cannot open: " + p.string());
    return { std::istreambuf_iterator<char>(f), {} };
}

static std::string to_lower(std::string s)
{
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::vector<uint8_t> decompress_if_needed(const uint8_t* data, size_t size)
{
    if (gc_yaz0_is_compressed(data, size)) {
        uint8_t* out   = nullptr;
        size_t   out_n = 0;
        if (gc_yaz0_decompress(data, size, &out, &out_n) != 0)
            throw std::runtime_error("yaz0 decompress failed");
        std::vector<uint8_t> result(out, out + out_n);
        free(out);
        return result;
    }
    return { data, data + size };
}

static void load_blos_from_arc(const fs::path& arc_path,
                                std::unordered_map<std::string, BLO>& blo_map)
{
    auto arc_bytes = read_file(arc_path);
    auto decompressed = decompress_if_needed(arc_bytes.data(), arc_bytes.size());
    GCArc* arc = gc_arc_open_mem(decompressed.data(), decompressed.size());
    if (!arc) {
        std::cerr << "failed to open arc: " << arc_path.filename() << "\n";
        return;
    }

    int count = gc_arc_entry_count(arc);
    for (int i = 0; i < count; ++i) {
        const GCEntry* entry = gc_arc_entry(arc, i);
        if (!entry || !entry->name) { 
            std::cerr << "Arc failed to load or has no entry\n"; 
            continue; 
        }

        std::string name = entry->name;
        if (to_lower(name).rfind(".blo") == std::string::npos) continue;
        if (to_lower(name) == "file_error.blo") continue;

        void*  raw  = nullptr;
        size_t size = 0;
        if (gc_arc_read_file(arc, i, &raw, &size) != 0) {
            std::cerr << "failed to read " << name << " from arc\n";
            continue;
        }

        try {
            auto bytes = decompress_if_needed(static_cast<const uint8_t*>(raw), size);
            free(raw);
            raw = nullptr;
            BLO blo  = parse_blo(bytes);
            blo.name = name;
            blo_map.emplace(to_lower(name), std::move(blo));
        } catch (const std::exception& e) {
            if (raw) free(raw);
            std::cerr << "failed to parse " << name << ": " << e.what() << "\n";
        }
    }

    gc_arc_close(arc);
}

static void finalize_blo(BLO& blo)
{
    blo.padding.clear();
    auto tmp = serialize_blo(blo);
    size_t i = 0;
    while (tmp.size() % 16 != 0) {
        blo.padding.push_back(PADDING_BYTES[i % PADDING_LENGTH]);
        tmp.push_back(PADDING_BYTES[i % PADDING_LENGTH]);
        ++i;
    }
    blo.size = static_cast<uint32_t>(tmp.size() - 32);
}

std::vector<PatchResult> process_layout(
    const fs::path& layout_folder,
    const fs::path& patch_folder,
    const fs::path& output_folder)
{
    std::unordered_map<std::string, BLO> blo_map;

    for (const auto& entry : fs::directory_iterator(layout_folder)) {
        if (!entry.is_regular_file()) continue;
        auto ext = to_lower(entry.path().extension().string());

        if (ext == ".arc") {
            try {
                load_blos_from_arc(entry.path(), blo_map);
            } catch (const std::exception& e) {
                std::cerr << "arc error " << entry.path().filename()
                          << ": " << e.what() << "\n";
            }
        } else if (ext == ".blo") {
            std::string name = entry.path().filename().string();
            if (to_lower(name) == "file_error.blo") continue;
            try {
                auto raw = read_file(entry.path());
                BLO blo  = parse_blo(raw);
                blo.name = name;
                blo_map.emplace(to_lower(name), std::move(blo));
            } catch (const std::exception& e) {
                std::cerr << "blo error " << name << ": " << e.what() << "\n";
            }
        }
    }

    for (auto& [key, blo] : blo_map) {
        try {
            write_file(output_folder / blo.name, serialize_blo(blo));
        } catch (const std::exception& e) {
            std::cerr << "serialize error " << blo.name << ": " << e.what() << "\n";
        }
    }

    std::vector<fs::path> json_paths;
    for (const auto& entry : fs::directory_iterator(patch_folder)) {
        if (entry.is_regular_file() &&
            to_lower(entry.path().extension().string()) == ".json")
            json_paths.push_back(entry.path());
    }

    std::unordered_map<std::string, GCArc*> open_arcs;

    auto get_arc = [&](const std::string& arc_name) -> GCArc* {
        auto it = open_arcs.find(arc_name);
        if (it != open_arcs.end()) return it->second;

        for (const auto& entry : fs::directory_iterator(layout_folder)) {
            if (!entry.is_regular_file()) continue;
            if (to_lower(entry.path().extension().string()) != ".arc") continue;
            if (entry.path().filename().string().find(arc_name) == std::string::npos)
                continue;

            auto bytes = read_file(entry.path());
            GCArc* arc = gc_arc_open_mem(bytes.data(), bytes.size());
            if (arc) {
                open_arcs[arc_name] = arc;
                return arc;
            }
        }
        return nullptr;
    };

    std::vector<PatchResult> results;

    for (const auto& json_path : json_paths) {
        std::string json_text;
        try { json_text = read_text(json_path); }
        catch (const std::exception& e) {
            std::cerr << "cannot read patch " << json_path.filename()
                      << ": " << e.what() << "\n";
            continue;
        }

        PatchDocument patch;
        try { patch = parse_patch_document(json_text); }
        catch (const std::exception& e) {
            std::cerr << "bad patch json " << json_path.filename()
                      << ": " << e.what() << "\n";
            continue;
        }

        auto it = blo_map.find(to_lower(patch.header.blo_file));
        if (it == blo_map.end()) {
            std::cerr << "patch " << json_path.filename()
                      << " targets unknown blo '" << patch.header.blo_file << "'\n";
            continue;
        }

        BLO blo = it->second;

        try { apply_patch(blo, patch); }
        catch (const std::exception& e) {
            std::cerr << "patch failed " << json_path.filename()
                      << ": " << e.what() << "\n";
            continue;
        }

        finalize_blo(blo);

        std::vector<uint8_t> out_bytes;
        try { out_bytes = serialize_blo(blo); }
        catch (const std::exception& e) {
            std::cerr << "serialize failed after patch " << json_path.filename()
                      << ": " << e.what() << "\n";
            continue;
        }

        std::string out_name = patch.header.new_filename.empty()
                               ? patch.header.blo_file
                               : patch.header.new_filename;

        try { write_file(output_folder / out_name, out_bytes); }
        catch (const std::exception& e) {
            std::cerr << "write failed " << out_name << ": " << e.what() << "\n";
            continue;
        }

        if (patch.header.action == "add" && !patch.header.arc.empty()) {
            GCArc* arc = get_arc(patch.header.arc);
            if (arc) {
                gc_arc_add_file(arc, "scrn",
                                out_name.c_str(),
                                out_bytes.data(),
                                out_bytes.size());
            } else {
                std::cerr << "arc '" << patch.header.arc << "' not found for "
                          << out_name << "\n";
            }
        }

        std::cout << "built: " << out_name << "\n";

        results.push_back({ out_name, patch.header.arc,
                            patch.header.action, patch.header.blo_file,
                            out_bytes });
    }

    for (auto& [arc_name, arc] : open_arcs) {
        void*  saved      = nullptr;
        size_t saved_size = 0;
        if (gc_arc_save(arc, &saved, &saved_size) == 0) {
            for (const auto& entry : fs::directory_iterator(layout_folder)) {
                if (!entry.is_regular_file()) continue;
                if (to_lower(entry.path().extension().string()) != ".arc") continue;
                if (entry.path().filename().string().find(arc_name) == std::string::npos)
                    continue;
                write_file(entry.path(),
                           { static_cast<uint8_t*>(saved),
                             static_cast<uint8_t*>(saved) + saved_size });
                break;
            }
            free(saved);
        }
        gc_arc_close(arc);
    }

    return results;
}

} // namespace Reservoir