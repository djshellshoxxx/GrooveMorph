#pragma once

#include "core/OperationParams.h"
#include "core/variation/VariationPlanner.h"

#include <cstdint>

namespace groovemorph::core::variation
{
// User-facing generator settings (A03/C02/C03/F08). A candidate is a pure function of
// (source scene, GeneratorSettings), so any candidate can be reproduced exactly later.
struct GeneratorSettings
{
    std::uint64_t seed = 1;
    Style style = Style::Conservative;
    float distance = 1.0f;         // C02: 0 = source, 1 = full planner candidate
    std::uint32_t candidate = 0;   // regenerate = increment
    std::uint32_t protectedOperations = 0; // extra "never touch" operations (bit i)
};

struct Candidate
{
    VariationSettings settings{};
    std::uint32_t changedOperations = 0;   // operations whose enable/depth/slice differ from source
    std::uint32_t permissionRestores = 0;  // fields restored by C03 locks
};

// Planner -> distance blend (C02) -> permission enforcement (C03) -> sanitize. Never mutates the
// source. Deterministic and allocation-free.
[[nodiscard]] Candidate buildCandidate(const VariationSettings& source,
                                       const GeneratorSettings& settings) noexcept;
} // namespace groovemorph::core::variation
