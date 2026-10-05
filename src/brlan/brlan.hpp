#pragma once

#ifndef RESERVOIR_BRLAN_H
#define RESERVOIR_BRLAN_H

#include "brlyt/brlyt.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Reservoir::Brlan {

using NamePairs = std::vector<std::pair<std::string, std::string>>;

size_t cloneTracks(Brlyt::Document& doc, const NamePairs& names);

} // namespace Reservoir::Brlan

#endif
