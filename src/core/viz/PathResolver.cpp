#include "core/viz/PathResolver.h"

#include "core/OperationParams.h"
#include "core/dsp/StageGeometry.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::viz
{
namespace
{
using trace::TraceFrame;

constexpr double hop = static_cast<double>(trace::hopSamples);

// Chain order walked backwards after the master mix.
constexpr OperationType backwardStages[] = {OperationType::TapeStop, OperationType::HalfTime, OperationType::Gate,
                                            OperationType::ReverseSlice, OperationType::Repeat};
} // namespace

std::size_t nearestFrame(const FrameWindow& w, double sample) noexcept
{
    if (w.frames == nullptr || w.count == 0 || !std::isfinite(sample))
        return noFrame;
    const auto* first = w.frames;
    const auto* last = w.frames + w.count;
    const auto* it = std::lower_bound(first, last, sample,
                                      [](const TraceFrame& f, double s) { return static_cast<double>(f.sample) < s; });
    std::size_t best = static_cast<std::size_t>(it - first);
    if (best == w.count)
        best = w.count - 1;
    else if (best > 0
             && std::abs(static_cast<double>(w.frames[best - 1].sample) - sample)
                    <= std::abs(static_cast<double>(w.frames[best].sample) - sample))
        --best;
    return std::abs(static_cast<double>(w.frames[best].sample) - sample) <= hop ? best : noFrame;
}

std::optional<double> beatAtSample(const FrameWindow& w, double sample) noexcept
{
    const std::size_t i = nearestFrame(w, sample);
    if (i == noFrame)
        return std::nullopt;
    const TraceFrame& f = w.frames[i];
    if ((f.flags & trace::TimingOk) == 0 || !(f.samplesPerBeat > 0.0f))
        return std::nullopt;
    return f.beat + (sample - static_cast<double>(f.sample)) / static_cast<double>(f.samplesPerBeat);
}

std::optional<double> sampleAtBeat(const FrameWindow& w, double beat) noexcept
{
    if (w.frames == nullptr || w.count == 0 || !std::isfinite(beat))
        return std::nullopt;
    const auto* first = w.frames;
    const auto* last = w.frames + w.count;
    const auto* it = std::lower_bound(first, last, beat, [](const TraceFrame& f, double b) { return f.beat < b; });
    std::size_t best = static_cast<std::size_t>(it - first);
    if (best == w.count)
        best = w.count - 1;
    else if (best > 0 && std::abs(w.frames[best - 1].beat - beat) <= std::abs(w.frames[best].beat - beat))
        --best;
    const TraceFrame& f = w.frames[best];
    if ((f.flags & trace::TimingOk) == 0 || !(f.samplesPerBeat > 0.0f))
        return std::nullopt;
    const double sample = static_cast<double>(f.sample) + (beat - f.beat) * static_cast<double>(f.samplesPerBeat);
    // Accept only beats within one hop of the recorded range.
    if (sample < static_cast<double>(w.frames[0].sample) - hop || sample > static_cast<double>(w.frames[w.count - 1].sample) + hop)
        return std::nullopt;
    return sample;
}

ResolvedThread resolveThread(const FrameWindow& w, double outputSample) noexcept
{
    ResolvedThread out;
    out.sourceSample = outputSample;
    std::size_t i = nearestFrame(w, outputSample);
    if (i == noFrame)
    {
        out.noData = true;
        return out;
    }
    const TraceFrame& head = w.frames[i];
    if ((head.flags & trace::TimingOk) == 0)
        return out; // transport stopped / timing invalid: nothing is being transformed, weight 0

    // Master mix: below 0.5 the dry path dominates and the thread is simply live.
    const float mix = head.intensity;
    if (mix < 0.5f)
    {
        out.weight = 1.0f;
        return out;
    }
    float weight = std::max(mix, 1.0f - mix);

    double tau = outputSample;
    for (const OperationType op : backwardStages)
    {
        i = nearestFrame(w, tau);
        if (i == noFrame)
        {
            out.noData = true;
            out.sourceSample = tau;
            out.weight = weight;
            return out;
        }
        const TraceFrame& f = w.frames[i];
        const std::size_t o = static_cast<std::size_t>(op);
        const float a = std::clamp(f.amount[o], 0.0f, 1.0f);
        if (op == OperationType::Gate)
        {
            weight *= 1.0f - a + a * std::clamp(f.gateGain, 0.0f, 1.0f);
            continue;
        }
        const int slot = stage::historySlot(o);
        if (a >= 0.5f)
        {
            // Lag grows by (1 - rate) per sample (reverse 2, half-time 0.5, repeat 0), so extrapolate the
            // frame's lag to the exact sample instead of snapping to the frame (up to a hop away).
            const double lag = std::max(0.0, static_cast<double>(f.lagSamples[slot])
                                                 + (tau - static_cast<double>(f.sample)) * (1.0 - static_cast<double>(f.rate[slot])));
            if (lag > 0.5 || f.rate[slot] != 1.0f)
                out.wetMask = static_cast<std::uint8_t>(out.wetMask | (1u << o));
            tau -= lag;
            weight *= a;
        }
        else
        {
            weight *= 1.0f - a;
        }
    }
    out.sourceSample = tau;
    out.weight = std::clamp(weight, 0.0f, 1.0f);
    // The source moment itself must lie inside the recorded window, otherwise it cannot be located.
    out.noData = nearestFrame(w, tau) == noFrame;
    return out;
}
} // namespace groovemorph::core::viz
