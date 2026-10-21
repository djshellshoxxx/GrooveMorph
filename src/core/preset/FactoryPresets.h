#pragma once

#include "core/preset/Preset.h"

#include <vector>

namespace groovemorph::core::preset
{
// Category names used by the factory table, in display order.
inline constexpr const char* factoryCategories[] = {"Init",           "Stutter",       "Halftime",     "Gate Chops",
                                                    "Tape Stops",     "Builds & Fills", "Subtle Groove", "Experimental"};

// Deterministic table built once (function-local static). Every entry has factory == true.
[[nodiscard]] const std::vector<Preset>& factoryPresets();
} // namespace groovemorph::core::preset
