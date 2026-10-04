#include "reservoir/reservoir.hpp"

#include "brlan/brlan.hpp"
#include "brlyt/brlyt.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace Reservoir {

namespace {

using Json  = nlohmann::json;
using Names = std::vector<std::pair<std::string, std::string>>;

struct PaneSet {
    std::string          pane;
    std::optional<float> translateX;
    std::optional<float> translateY;
    std::optional<float> translateZ;
    std::optional<float> width;
    std::optional<float> height;
};

struct CloneOp {
    std::string          source;
    std::string          parent;
    Names                rename;
    std::vector<PaneSet> set;
};

struct Patch {
    std::string          archive;
    std::string          layout;
    std::vector<CloneOp> clones;
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::optional<float> optionalFloat(const Json& json, const char* key) {
    if (!json.contains(key)) {
        return std::nullopt;
    }
    return json.at(key).get<float>();
}

PaneSet parsePaneSet(const Json& json) {
    PaneSet set;
    set.pane       = json.at("pane").get<std::string>();
    set.translateX = optionalFloat(json, "translate_x");
    set.translateY = optionalFloat(json, "translate_y");
    set.translateZ = optionalFloat(json, "translate_z");
    set.width      = optionalFloat(json, "width");
    set.height     = optionalFloat(json, "height");
    return set;
}

CloneOp parseClone(const Json& json) {
    CloneOp op;
    op.source = json.at("source").get<std::string>();
    op.parent = json.value("parent", std::string());
    for (const Json& rule : json.at("rename")) {
        op.rename.emplace_back(rule.at("from").get<std::string>(), rule.at("to").get<std::string>());
    }
    if (json.contains("set")) {
        for (const Json& set : json.at("set")) {
            op.set.push_back(parsePaneSet(set));
        }
    }
    return op;
}

void applySet(Brlyt::Document& doc, const PaneSet& set) {
    const size_t index = Brlyt::findPane(doc, set.pane);
    if (index == Brlyt::kNoPane) {
        throw std::runtime_error("pane not found for set: " + set.pane);
    }

    Brlyt::Chunk& chunk = doc.chunks[index];
    if (set.translateX) Brlyt::setPaneFloat(chunk, Brlyt::kPaneTranslateOffset, *set.translateX);
    if (set.translateY) Brlyt::setPaneFloat(chunk, Brlyt::kPaneTranslateOffset + 4, *set.translateY);
    if (set.translateZ) Brlyt::setPaneFloat(chunk, Brlyt::kPaneTranslateOffset + 8, *set.translateZ);
    if (set.width) Brlyt::setPaneFloat(chunk, Brlyt::kPaneSizeOffset, *set.width);
    if (set.height) Brlyt::setPaneFloat(chunk, Brlyt::kPaneSizeOffset + 4, *set.height);
}

} // namespace

struct LayoutPatches::Impl {
    std::vector<Patch> patches;

    std::mutex                   mutex;
    std::map<std::string, Names> clonedNames;
};

LayoutPatches::LayoutPatches() : impl_(std::make_unique<Impl>()) {}

LayoutPatches::~LayoutPatches() = default;

void LayoutPatches::add(const std::string& json_text) {
    const Json json = Json::parse(json_text);

    Patch patch;
    patch.archive = json.at("archive").get<std::string>();
    patch.layout  = json.at("layout").get<std::string>();
    for (const Json& clone : json.at("clone")) {
        patch.clones.push_back(parseClone(clone));
    }

    impl_->patches.push_back(std::move(patch));
}

bool LayoutPatches::empty() const {
    return impl_->patches.empty();
}

std::optional<std::vector<uint8_t>> LayoutPatches::patch_layout(
    const std::string& archive, const std::string& file, const uint8_t* data, size_t size) const {
    std::optional<Brlyt::Document> doc;
    Names                          names;

    for (const Patch& patch : impl_->patches) {
        if (lower(patch.archive) != lower(archive) || lower(patch.layout) != lower(file)) {
            continue;
        }

        try {
            if (!doc) {
                doc = Brlyt::parse(std::vector<uint8_t>(data, data + size));
            }
            for (const CloneOp& op : patch.clones) {
                const Brlyt::CloneResult clone = Brlyt::cloneSubtree(*doc, op.source, op.rename, op.parent);
                names.insert(names.end(), clone.renamed.begin(), clone.renamed.end());
                for (const PaneSet& set : op.set) {
                    applySet(*doc, set);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "layout patch failed for " << archive << "/" << file << ": " << e.what() << "\n";
            return std::nullopt;
        }
    }

    if (!doc) {
        return std::nullopt;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->clonedNames[lower(archive)] = names;
    }

    return Brlyt::serialize(*doc);
}

std::optional<std::vector<uint8_t>> LayoutPatches::patch_animation(
    const std::string& archive, const uint8_t* data, size_t size) const {
    Names names;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->clonedNames.find(lower(archive));
        if (it == impl_->clonedNames.end()) {
            return std::nullopt;
        }
        names = it->second;
    }

    try {
        Brlyt::Document doc = Brlyt::parse(std::vector<uint8_t>(data, data + size));
        if (Brlan::cloneTracks(doc, names) == 0) {
            return std::nullopt;
        }
        return Brlyt::serialize(doc);
    } catch (const std::exception& e) {
        std::cerr << "animation patch failed for " << archive << ": " << e.what() << "\n";
        return std::nullopt;
    }
}

} // namespace Reservoir
