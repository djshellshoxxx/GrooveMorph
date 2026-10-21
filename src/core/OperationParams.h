#pragma once

#include "core/control/SceneControl.h"

#include <array>
#include <cstddef>

namespace groovemorph::core
{
// Rhythmic transformations in the MVP (see SPECIFICATION.md core MVP row).
enum class OperationType : std::size_t
{
    Repeat,       // repeat/stutter a short slice
    ReverseSlice, // play a bounded slice backwards
    Gate,         // rhythmic volume gate
    HalfTime,     // half-speed playback feel
    TapeStop,     // tape-stop-style rate slowdown curve
    FilterSweep,  // beat-synced resonant low-pass sweep across each slice (no history)
    BitCrush,     // slice-synced sample-rate + bit-depth reducer (no history)
    Count
};

inline constexpr std::size_t operationCount = static_cast<std::size_t>(OperationType::Count);
static_assert(control::laneCount == operationCount, "SceneControl lane count must match the operation count");
inline constexpr std::size_t variationCount = 4;

struct OperationSettings
{
    bool enabled = false;
    float depth = 1.0f;          // 0..1 per-operation amount
    float sliceBeats = 0.25f;    // slice/gate length in meter beats, 1/16..4
};

struct VariationSettings
{
    std::array<OperationSettings, operationCount> operations{};
    // Phrase placement, event lane, permissions and activity limits (default = inert).
    control::SceneControl control{};

    OperationSettings& operator[](OperationType type) noexcept
    {
        return operations[static_cast<std::size_t>(type)];
    }
    const OperationSettings& operator[](OperationType type) const noexcept
    {
        return operations[static_cast<std::size_t>(type)];
    }
};
} // namespace groovemorph::core
