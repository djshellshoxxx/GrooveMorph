#pragma once

#include "core/OperationParams.h"

#include <cstdint>

// N05 "Surprise me": one constrained candidate built with the Generator defaults
// (docs/specs/N05-EASY-MODE.md section 5). Pure, deterministic, allocation-free.
namespace groovemorph::core::variation
{
inline constexpr std::uint32_t surpriseMaxAttempts = 32;
inline constexpr float surpriseMinAudibleDepth = 0.10f;

struct SurpriseResult
{
    bool ok = false;                    // false: nothing acceptable (everything locked, or no audible change)
    VariationSettings settings{};       // the stored scene when ok is false
    std::uint32_t candidate = 0;        // candidate number that was used
    std::uint32_t changedOperations = 0; // bit i: operation i differs from the stored scene
};

// True when at least one enabled operation has a finite depth >= surpriseMinAudibleDepth.
[[nodiscard]] bool hasAudibleEffect(const VariationSettings& settings) noexcept;

// Tries candidate numbers attempt .. attempt + surpriseMaxAttempts - 1 (GeneratorSettings defaults:
// Conservative style, distance 1, no event-pattern generation, no never-touch operations) and returns
// the first that differs from `stored` and is audible. Locks are enforced by the candidate builder.
// Never mutates `stored`.
[[nodiscard]] SurpriseResult buildSurprise(const VariationSettings& stored, std::uint64_t sceneSeed,
                                           std::uint32_t attempt) noexcept;
} // namespace groovemorph::core::variation
