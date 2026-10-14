#pragma once

#include "core/control/PlacementEvaluator.h"

#include <cstdint>

namespace groovemorph::core::control
{
// C04: where in its pattern a destination scene starts when a variation switch fires.
// Launch quantization decides WHEN; the phase policy decides WHERE. Positions are meter beats.
// PhaseConfig itself lives in SceneControl.h (it is part of the scene).

// Clamps to the documented domain (non-finite offset -> 0, enum out of range -> Preserve).
// Returns true when anything changed.
bool sanitize(PhaseConfig& config) noexcept;
[[nodiscard]] bool samePhase(const PhaseConfig& a, const PhaseConfig& b) noexcept;

// Pattern phase (meter beats inside [0, phraseBeats)) the destination scene has at the switch
// frame, given the global meter-beat position `positionBeats` there. Pure, finite and bounded.
[[nodiscard]] double resolveLocalPhase(const PhaseConfig& config, double positionBeats,
                                       const GridContext& grid) noexcept;

// Value added to the global meter-beat position to obtain the pattern position, in
// [0, phraseBeats). Equals 0 for Preserve with offset 0.
[[nodiscard]] double resolvePhaseShift(const PhaseConfig& config, double positionBeats,
                                       const GridContext& grid) noexcept;
} // namespace groovemorph::core::control
