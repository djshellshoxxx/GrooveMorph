#pragma once

#include "core/OperationParams.h"

#include <cstdint>

// N05 Chaos macro (see docs/specs/N05-EASY-MODE.md section 3). A pure, deterministic overlay that
// makes the SOUNDING scene busier as `chaos` rises: more operations enabled, higher depth, shorter
// slices and (for explicit event lanes) more events. The stored scene is never edited; the caller
// applies the result only to the audio-thread copy, like the morph blend.
namespace groovemorph::core::control
{
struct ChaosContext
{
    float chaos = 0.0f;            // 0..1; 0, NaN and negative values return the scene unchanged
    std::uint64_t seed = 0;        // scene bank seed
    std::uint32_t sceneIndex = 0;  // which scene is sounding (changes the draws)
    std::int64_t phraseTicks = 0;  // phrase length in ticks; <= 0 disables added events
    std::uint32_t neverTouchMask = 0; // bit i: operation i is left exactly as stored
};

// Real-time safe: noexcept, no allocation, bounded loops. Respects the enable/depth/slice/events lock
// bits, per-event locks, the placement lock, protected regions and the never-touch mask. Activity
// (enabled count, depth, 1/slice, event count) never decreases as chaos increases for a fixed context.
[[nodiscard]] VariationSettings applyChaos(const VariationSettings& scene, const ChaosContext& context) noexcept;
} // namespace groovemorph::core::control
