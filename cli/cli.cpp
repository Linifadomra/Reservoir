#include "reservoir/reservoir.hpp"
#include "CLI11.hpp"
#include <iostream>

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

    try {
        auto results = Reservoir::process_layout(layout_folder, patch_folder, output_folder);
        std::cout << "Done. Processed " << results.size() << " patch(es).\n";
        for (const auto& r : results)
            std::cout << "  built: " << r.new_filename << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}