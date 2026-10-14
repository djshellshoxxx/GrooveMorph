#pragma once

#include "core/trace/TraceFrame.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

// Maps what is heard back to the moment it was originally played (docs/visual-feedback/SPEC.md
// sections 5 and 7). Pure and allocation-free; consumes frames copied out of a TraceRing.
namespace groovemorph::core::viz
{
inline constexpr std::size_t noFrame = std::numeric_limits<std::size_t>::max();

// Ascending frames of ONE epoch (see trace::latestEpochStart).
struct FrameWindow
{
    const trace::TraceFrame* frames = nullptr;
    std::size_t count = 0;
};

struct ResolvedThread
{
    double sourceSample = 0.0;  // engine-clock sample of the input this output moment came from
    float weight = 0.0f;        // 0..1: share of the heard sound carried by this path
    std::uint8_t wetMask = 0;   // bit per OperationType: history stage that actually moved the path
    float gateFactor = 1.0f;    // gate multiplier applied on this path (1 = open), at the moment the gate acted
    bool noData = false;        // the chain walked outside the recorded window
};

// Frame nearest to `sample` on the engine clock; noFrame when the window is empty or the nearest
// frame is more than one hop away.
[[nodiscard]] std::size_t nearestFrame(const FrameWindow& w, double sample) noexcept;

// Meter-beat position of an engine sample (nullopt without a timed frame nearby).
[[nodiscard]] std::optional<double> beatAtSample(const FrameWindow& w, double sample) noexcept;

// Engine sample at a meter-beat position (nullopt outside the window or without timing).
[[nodiscard]] std::optional<double> sampleAtBeat(const FrameWindow& w, double beat) noexcept;

// Follows the signal chain backwards from an output sample: Mix, TapeStop, HalfTime, Gate, Reverse,
// Repeat. A stage with applied depth >= 0.5 is followed through its read position (dominant-branch
// rule); otherwise the dry branch is taken. Gate multiplies the weight by (1 - a + a * gateGain).
[[nodiscard]] ResolvedThread resolveThread(const FrameWindow& w, double outputSample) noexcept;
} // namespace groovemorph::core::viz
