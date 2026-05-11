#include "reservoir/reservoir.hpp"
#include "CLI11.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    CLI::App app{"Reservoir CLI: BLO Patcher"};

    std::string layout_folder;
    std::string patch_folder;
    std::string output_folder;

    app.add_option("layout", layout_folder, "Path to the layout folder containing .arc/.blo files")
        ->required()
        ->check(CLI::ExistingDirectory);
    app.add_option("patches", patch_folder, "Path to the folder containing .json patch files")
        ->required()
        ->check(CLI::ExistingDirectory);
    app.add_option("output", output_folder, "Path to the output folder")
        ->required();

    CLI11_PARSE(app, argc, argv);

    std::vector<std::string> patch_jsons;
    for (const auto& entry : fs::directory_iterator(patch_folder)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (ext != ".json" && ext != ".JSON") continue;

        std::ifstream f(entry.path());
        if (!f) {
            std::cerr << "Warning: could not open " << entry.path().filename() << ", skipping.\n";
            continue;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        patch_jsons.push_back(ss.str());
    }

    std::cout << "Loaded " << patch_jsons.size() << " patch file(s).\n";

    try {
        auto results = Reservoir::process_layout(layout_folder, output_folder, patch_jsons);
        std::cout << "Done. Processed " << results.size() << " patch(es).\n";
        for (const auto& r : results)
            std::cout << "  built: " << r.new_filename << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}