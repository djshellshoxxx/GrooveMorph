#pragma once

#include "core/OperationParams.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Planned (future) paths for the Warp Weave, from the same StageGeometry maths the engine uses
// (docs/visual-feedback/SPEC.md section 6). Assumes constant tempo over the planned span.
namespace groovemorph::core::viz
{
// Effective scene settings for a half-open beat range [startBeat, endBeat). The UI layer builds these
// from the committed scene and the placement evaluator; the planner never reads a model itself.
struct PlanSegment
{
    double startBeat = 0.0;
    double endBeat = 0.0;
    VariationSettings settings{};
};

struct PlanContext
{
    const PlanSegment* segments = nullptr;
    std::size_t segmentCount = 0;
    float intensity = 1.0f;            // master intensity
    double samplesPerBeat = 0.0;       // from the newest timed frame
    std::size_t historySamples = 0;    // ceil(maxHistorySeconds * sampleRate)
    std::array<double, operationCount> phaseOffsetBeats{};
};

struct PlannedThread
{
    double sourceBeat = 0.0;   // beat of the input this output moment will come from
    float weight = 0.0f;       // 0..1, same rule as PathResolver
    std::uint8_t wetMask = 0;  // bit per OperationType that moves the path
    float gateFactor = 1.0f;   // gate multiplier on the path (1 = open)
    bool valid = false;        // false when no segment covers a visited beat or timing is unusable
};

// Mirrors resolveThread: master mix, then TapeStop, HalfTime, Gate, Reverse, Repeat, taking each stage's
// settings from the segment covering the beat at which that stage acts.
[[nodiscard]] PlannedThread planThread(const PlanContext& ctx, double outputBeat) noexcept;
} // namespace groovemorph::core::viz
