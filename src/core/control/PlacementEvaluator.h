#pragma once

#include "core/control/SceneControl.h"

#include <array>
#include <cstdint>
#include <limits>

namespace groovemorph::core::control
{
struct GridContext
{
    int beatsPerBar = 4;   // meter numerator
    int phraseBars = 4;
    [[nodiscard]] std::int64_t phraseTicks() const noexcept
    {
        return static_cast<std::int64_t>(beatsPerBar) * phraseBars * ticksPerBeat;
    }
};

// Operation targets at one musical position. Evaluated with continuous (double) meter-beat
// positions so that block and segment boundaries never move an event.
struct Targets
{
    std::array<bool, laneCount> active{};
    std::array<float, laneCount> depthScale{};
    // Event-local slice in meter beats; 0 = keep the operation's own slice.
    std::array<float, laneCount> sliceBeats{};
    // Absolute meter-beat position the operation's beat grid is measured from; 0 = shared grid.
    // Non-zero only inside an event with restartCycle set (the cycle restarts at the onset).
    std::array<double, laneCount> phaseOffsetBeats{};
    // Ticks (1/960 meter beat) until anything in `active`/`depthScale` can change.
    // Infinite (1e18) when the scene control is inert.
    double ticksUntilChange = 1.0e18;
    bool gated = false; // false: control is inert and targets are the identity
};

// True when evaluate() would ever alter the targets. Cheap pre-check for the callback.
[[nodiscard]] bool isActive(const SceneControl& control) noexcept;

// Real-time safe: noexcept, no allocation, bounded loops (<= maxLaneEvents).
// positionBeats: meter beats since the grid origin (negative before it).
// Placement gates operations before the engine chain (C01); activity limits (C06) keep the
// `maxConcurrent` highest-priority gated-on operations (ties: lower operation index).
// enabledMask: bit i set when operation i is enabled in the scene; only those count toward
// the activity limit.
// globalBeats (optional, finite): the un-shifted transport position. When a C04 phase shift moves
// the pattern (positionBeats = pattern position), the phrase cadence (Every/Offset) and bar-start
// protection follow this global position; NaN (default) means positionBeats is the global position.
[[nodiscard]] Targets evaluate(const SceneControl& control, const GridContext& grid,
                               double positionBeats, std::uint32_t enabledMask = 0x1fu,
                               double globalBeats = std::numeric_limits<double>::quiet_NaN()) noexcept;
} // namespace groovemorph::core::control
