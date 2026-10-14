#include "core/viz/PlannedPath.h"

#include "core/dsp/StageGeometry.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::viz
{
namespace
{
constexpr OperationType backwardStages[] = {OperationType::TapeStop, OperationType::HalfTime, OperationType::Gate,
                                            OperationType::ReverseSlice, OperationType::Repeat};

const PlanSegment* segmentAt(const PlanContext& ctx, double beat) noexcept
{
    for (std::size_t i = 0; i < ctx.segmentCount; ++i)
        if (beat >= ctx.segments[i].startBeat && beat < ctx.segments[i].endBeat)
            return &ctx.segments[i];
    return nullptr;
}
} // namespace

PlannedThread planThread(const PlanContext& ctx, double outputBeat) noexcept
{
    PlannedThread out;
    out.sourceBeat = outputBeat;
    if (ctx.segments == nullptr || ctx.segmentCount == 0 || !(ctx.samplesPerBeat > 0.0) || !std::isfinite(outputBeat))
        return out;

    const float mix = stage::sanitiseUnit(ctx.intensity, 0.0f);
    if (segmentAt(ctx, outputBeat) == nullptr)
        return out;
    out.valid = true;
    if (mix < 0.5f)
    {
        out.weight = 1.0f;
        return out;
    }
    float weight = std::max(mix, 1.0f - mix);

    double beat = outputBeat;
    for (const OperationType op : backwardStages)
    {
        const PlanSegment* seg = segmentAt(ctx, beat);
        if (seg == nullptr)
        {
            out.valid = false;
            out.sourceBeat = beat;
            out.weight = weight;
            return out;
        }
        const std::size_t o = static_cast<std::size_t>(op);
        const OperationSettings& s = seg->settings.operations[o];
        const float a = s.enabled ? stage::sanitiseUnit(s.depth, 0.0f) : 0.0f;
        const stage::Slice slice = stage::effectiveSlice(op, s.sliceBeats, ctx.samplesPerBeat, ctx.historySamples);
        const double phased = beat - ctx.phaseOffsetBeats[o];
        if (op == OperationType::Gate)
        {
            const bool open = a <= 0.0f || stage::gateOpen(phased, slice.beats, ctx.samplesPerBeat, slice.samples);
            const float factor = 1.0f - a + (open ? a : 0.0f);
            weight *= factor;
            out.gateFactor = factor;
            continue;
        }
        if (a >= 0.5f)
        {
            // Lag relative to engine time 0: readPosition returns r = t - lag.
            const stage::ReadState rs = stage::readPosition(op, 0.0, phased, slice.beats, ctx.samplesPerBeat, slice.samples);
            const double lag = std::max(0.0, -rs.r);
            if (lag > 0.5 || rs.rate != 1.0)
                out.wetMask = static_cast<std::uint8_t>(out.wetMask | (1u << o));
            beat -= lag / ctx.samplesPerBeat;
            weight *= a;
        }
        else
        {
            weight *= 1.0f - a;
        }
    }
    out.sourceBeat = beat;
    out.weight = std::clamp(weight, 0.0f, 1.0f);
    return out;
}
} // namespace groovemorph::core::viz
