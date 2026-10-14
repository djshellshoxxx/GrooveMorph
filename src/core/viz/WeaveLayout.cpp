#include "core/viz/WeaveLayout.h"

#include "core/OperationParams.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::viz
{
namespace
{
constexpr float gateClosedBelow = 0.25f;

float normalised(double beat, const WeaveParams& p) noexcept
{
    const double lo = p.viewStartBeat - p.leadBeats;
    return static_cast<float>(std::clamp((beat - lo) / (p.spanBeats + p.leadBeats), 0.0, 1.0));
}

Effect primaryOf(std::uint8_t mask) noexcept
{
    // Priority when several stages moved the thread: TapeStop, HalfTime, Reverse, Repeat.
    if (mask & (1u << static_cast<unsigned>(OperationType::TapeStop))) return Effect::TapeStop;
    if (mask & (1u << static_cast<unsigned>(OperationType::HalfTime))) return Effect::HalfTime;
    if (mask & (1u << static_cast<unsigned>(OperationType::ReverseSlice))) return Effect::Reverse;
    if (mask & (1u << static_cast<unsigned>(OperationType::Repeat))) return Effect::Repeat;
    return Effect::Live;
}
} // namespace

std::size_t buildWeave(const FrameWindow& w, const WeaveParams& p, Thread* out, std::size_t capacity) noexcept
{
    if (out == nullptr || capacity == 0 || !(p.spanBeats > 0.0) || !(p.leadBeats >= 0.0))
        return 0;
    const std::size_t n = std::min<std::size_t>(capacity, static_cast<std::size_t>(std::clamp(p.threads, 1, 256)));
    std::size_t written = 0;
    for (std::size_t j = 0; j < n; ++j)
    {
        const double beat = p.viewStartBeat + (static_cast<double>(j) + 0.5) / static_cast<double>(n) * p.spanBeats;
        if (beat > p.nowBeat)
            break; // the rest is the future: drawn by the planner, never invented here
        Thread t;
        t.xTop = normalised(beat, p);
        t.xBottom = t.xTop;
        const auto sample = sampleAtBeat(w, beat);
        if (!sample)
        {
            t.noData = true;
            t.opacity = 0.1f;
            out[written++] = t;
            continue;
        }
        const ResolvedThread r = resolveThread(w, *sample);
        const auto srcBeat = r.noData ? std::nullopt : beatAtSample(w, r.sourceSample);
        if (!srcBeat)
        {
            t.noData = true;
            t.opacity = 0.1f;
            out[written++] = t;
            continue;
        }
        t.xBottom = normalised(*srcBeat, p);
        t.clipped = *srcBeat < p.viewStartBeat - p.leadBeats;
        t.effectMask = r.wetMask;
        t.primary = primaryOf(r.wetMask);
        const std::size_t fi = nearestFrame(w, *sample);
        if (fi != noFrame)
        {
            const auto& f = w.frames[fi];
            t.gateClosed = f.amount[static_cast<std::size_t>(OperationType::Gate)] >= 0.5f && f.gateGain < gateClosedBelow;
        }
        t.opacity = std::clamp(0.18f + 0.62f * r.weight, 0.05f, 0.85f);
        t.width = 0.8f + 0.8f * r.weight;
        out[written++] = t;
    }
    return written;
}
} // namespace groovemorph::core::viz
