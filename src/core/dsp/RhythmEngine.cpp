#include "core/dsp/RhythmEngine.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core
{
namespace
{
constexpr std::size_t opIndex(OperationType type) noexcept
{
    return static_cast<std::size_t>(type);
}

using stage::delayMultiple;
using stage::historySlot;
using stage::positionInCell;
using stage::sanitiseSliceBeats;
using stage::sanitiseUnit;

float slew(float current, float target, float step) noexcept
{
    if (current < target)
        return std::min(target, current + step);
    if (current > target)
        return std::max(target, current - step);
    return target;
}
} // namespace

void RhythmEngine::prepare(double sampleRate, int maxBlockSize, int maxChannels)
{
    if (!(sampleRate > 0.0) || !std::isfinite(sampleRate) || maxChannels <= 0)
    {
        rings.clear();
        capacity = 0;
        historySamples = 0;
        channelCount = 0;
        return;
    }
    historySamples = static_cast<std::size_t>(std::ceil(maxHistorySeconds * sampleRate));
    capacity = historySamples + static_cast<std::size_t>(std::max(0, maxBlockSize)) + 8;
    channelCount = maxChannels;
    rings.assign(historyStageCount * static_cast<std::size_t>(channelCount) * capacity, 0.0f);
    fadeSamples = std::max(1, static_cast<int>(std::lround(crossfadeSeconds * sampleRate)));
    gateRamp = std::max(1, static_cast<int>(std::lround(gateRampSeconds * sampleRate)));
    reset();
}

void RhythmEngine::reset() noexcept
{
    std::fill(rings.begin(), rings.end(), 0.0f);
    now = 0;
    initialised = false;
    smoothedIntensity = 0.0f;
    amounts.fill(0.0f);
    voices.fill(Voice{});
    gateGain = 1.0f;
    warmingAfterReset = false;
    ++traceEpoch;
    traceInPeak = 0.0f;
    traceOutPeak = 0.0f;
}

void RhythmEngine::resetHistory() noexcept
{
    now = 0;
    voices.fill(Voice{});
    // Amounts of history operations drop at once: ramping them down would read the emptied history.
    for (std::size_t op = 0; op < operationCount; ++op)
        if (historySlot(op) >= 0)
            amounts[op] = 0.0f;
    warmingAfterReset = true;
    ++traceEpoch;
    traceInPeak = 0.0f;
    traceOutPeak = 0.0f;
}

double RhythmEngine::maxSliceSamples(OperationType type) const noexcept
{
    if (historySamples < 8)
        return 0.0;
    return static_cast<double>(historySamples - 4) / delayMultiple(type);
}

float RhythmEngine::readRing(const float* ring, std::int64_t time, double r) const noexcept
{
    const auto cap = static_cast<std::int64_t>(capacity);
    const double t = static_cast<double>(time);
    r = std::clamp(r, t - static_cast<double>(cap - 2), t);
    const double base = std::floor(r);
    const double frac = r - base;
    auto wrap = [cap](std::int64_t i) noexcept {
        std::int64_t m = i % cap;
        return static_cast<std::size_t>(m < 0 ? m + cap : m);
    };
    const auto i = static_cast<std::int64_t>(base);
    const float a = i < 0 ? 0.0f : ring[wrap(i)];
    if (frac <= 0.0)
        return a;
    const float b = i + 1 < 0 ? 0.0f : ring[wrap(i + 1)];
    return a + static_cast<float>(frac) * (b - a);
}

void RhythmEngine::process(float* const* channels,
                           int numChannels,
                           int numSamples,
                           const VariationSettings& variation,
                           float intensity,
                           const RhythmTiming& timing) noexcept
{
    if (!isPrepared() || channels == nullptr || numSamples <= 0 || numChannels <= 0)
        return;
    const int nch = std::min(numChannels, channelCount);

    const double spb = timing.samplesPerBeat;
    const bool timingOk = timing.isPlaying && std::isfinite(spb) && spb > 0.0
                          && std::isfinite(timing.beatAtBlockStart);

    std::array<float, operationCount> targets{};
    std::array<double, operationCount> sliceBeats{};
    std::array<double, operationCount> sliceSamples{};
    for (std::size_t op = 0; op < operationCount; ++op)
    {
        const auto& s = variation.operations[op];
        targets[op] = (timingOk && s.enabled) ? sanitiseUnit(s.depth, 0.0f) : 0.0f;
        double beats = sanitiseSliceBeats(s.sliceBeats);
        if (timingOk)
        {
            const stage::Slice slice = stage::effectiveSlice(static_cast<OperationType>(op), s.sliceBeats, spb, historySamples);
            beats = slice.beats;
            sliceSamples[op] = slice.samples;
        }
        sliceBeats[op] = beats;
        // After a history reset, a history operation waits until fresh audio covers its read delay.
        if (warmingAfterReset && historySlot(op) >= 0 && timingOk
            && static_cast<double>(now) < delayMultiple(static_cast<OperationType>(op)) * sliceSamples[op]
                                              + static_cast<double>(fadeSamples) + 2.0)
            targets[op] = 0.0f;
    }
    const float intensityTarget = sanitiseUnit(intensity, 0.0f);

    if (!initialised)
    {
        smoothedIntensity = intensityTarget;
        amounts = targets;
        initialised = true;
    }

    const float fadeStep = 1.0f / static_cast<float>(fadeSamples);
    const float gateStep = 1.0f / static_cast<float>(gateRamp);
    const std::size_t ringStride = static_cast<std::size_t>(channelCount) * capacity;
    const auto writeIndex = [this](std::int64_t t) noexcept {
        return static_cast<std::size_t>(t % static_cast<std::int64_t>(capacity));
    };

    const bool tracing = traceSink != nullptr && traceSink->active();
    std::array<double, historyStageCount> newR{};
    const float fadeNorm = 1.0f / static_cast<float>(fadeSamples + 1);

    for (int n = 0; n < numSamples; ++n)
    {
        smoothedIntensity = slew(smoothedIntensity, intensityTarget, fadeStep);
        for (std::size_t op = 0; op < operationCount; ++op)
            amounts[op] = slew(amounts[op], targets[op], fadeStep);

        const double beat = timingOk ? timing.beatAtBlockStart + static_cast<double>(n) / spb : 0.0;
        const double t = static_cast<double>(now);

        // Gate gain.
        {
            const std::size_t g = opIndex(OperationType::Gate);
            if (amounts[g] > 0.0f && timingOk)
            {
                const float target =
                    stage::gateOpen(beat - timing.phaseOffsetBeats[g], sliceBeats[g], spb, sliceSamples[g]) ? 1.0f : 0.0f;
                gateGain = slew(gateGain, target, gateStep);
            }
            else
            {
                gateGain = amounts[g] > 0.0f ? slew(gateGain, 1.0f, gateStep) : 1.0f;
            }
        }

        // Read positions of history-based operations.
        for (std::size_t op = 0; op < operationCount; ++op)
        {
            const int slot = historySlot(op);
            if (slot < 0)
                continue;
            Voice& v = voices[static_cast<std::size_t>(slot)];
            if (amounts[op] <= 0.0f)
            {
                v.valid = false;
                v.tapCount = 0;
                continue;
            }
            const double len = sliceSamples[op];
            const stage::ReadState read =
                stage::readPosition(timingOk ? static_cast<OperationType>(op) : OperationType::Count, t,
                                    beat - timing.phaseOffsetBeats[op], sliceBeats[op], spb, len);
            const double r = read.r;
            const double rate = read.rate;
            if (v.valid)
            {
                const double predicted = v.prevR + v.prevRate;
                if (std::abs(r - predicted) > 0.5)
                {
                    if (v.tapCount == static_cast<int>(v.taps.size()))
                    {
                        v.taps[0] = v.taps[1];
                        v.tapCount = 1;
                    }
                    v.taps[static_cast<std::size_t>(v.tapCount++)] =
                        Voice::Tap{predicted, v.prevRate, fadeSamples};
                }
            }
            v.prevR = r;
            v.prevRate = rate;
            v.valid = true;
            newR[static_cast<std::size_t>(slot)] = r;
        }

        const std::size_t w = writeIndex(now);
        for (int c = 0; c < nch; ++c)
        {
            float* data = channels[c];
            if (data == nullptr)
                continue;
            const float dry = std::isfinite(data[n]) ? data[n] : 0.0f; // never let NaN/Inf poison history
            if (tracing)
                traceInPeak = std::max(traceInPeak, std::abs(dry));
            float x = dry;
            for (std::size_t op = 0; op < operationCount; ++op)
            {
                const int slot = historySlot(op);
                const float amount = amounts[op];
                if (slot >= 0)
                {
                    const auto s = static_cast<std::size_t>(slot);
                    float* ring = rings.data() + s * ringStride + static_cast<std::size_t>(c) * capacity;
                    ring[w] = x;
                    if (amount > 0.0f && voices[s].valid)
                    {
                        const Voice& v = voices[s];
                        float wet = readRing(ring, now, newR[s]);
                        if (v.tapCount > 0)
                        {
                            float old = readRing(ring, now, v.taps[0].r);
                            for (int k = 1; k < v.tapCount; ++k)
                            {
                                const auto& prev = v.taps[static_cast<std::size_t>(k - 1)];
                                const float next = readRing(ring, now, v.taps[static_cast<std::size_t>(k)].r);
                                old = next + static_cast<float>(prev.remaining) * fadeNorm * (old - next);
                            }
                            const auto& last = v.taps[static_cast<std::size_t>(v.tapCount - 1)];
                            wet += static_cast<float>(last.remaining) * fadeNorm * (old - wet);
                        }
                        x += amount * (wet - x);
                    }
                }
                else if (amount > 0.0f)
                {
                    x += amount * (x * gateGain - x);
                }
            }
            float y = dry;
            if (smoothedIntensity > 0.0f && x != dry) // x == dry: no operation changed the signal
            {
                y = dry + smoothedIntensity * (x - dry);
                if (!std::isfinite(y) || std::abs(y) < 1.0e-30f)
                    y = 0.0f;
            }
            data[n] = y;
            if (tracing)
                traceOutPeak = std::max(traceOutPeak, std::abs(y));
        }

        for (auto& v : voices)
        {
            for (int k = 0; k < v.tapCount; ++k)
            {
                auto& tap = v.taps[static_cast<std::size_t>(k)];
                tap.r += tap.rate;
                --tap.remaining;
            }
            while (v.tapCount > 0 && v.taps[0].remaining <= 0)
            {
                v.taps[0] = v.taps[1];
                --v.tapCount;
            }
        }

        if (tracing && (now & (trace::hopSamples - 1)) == trace::hopSamples - 1)
        {
            trace::TraceFrame f;
            f.sample = now;
            f.epoch = traceEpoch;
            f.flags = (timingOk ? trace::TimingOk : 0u);
            f.beat = timingOk ? timing.beatAtBlockStart + static_cast<double>(n) / spb : 0.0;
            f.samplesPerBeat = timingOk ? static_cast<float>(spb) : 0.0f;
            f.intensity = smoothedIntensity;
            for (std::size_t op = 0; op < operationCount; ++op)
            {
                f.amount[op] = amounts[op];
                const int slot = historySlot(op);
                if (slot < 0)
                    continue;
                const Voice& v = voices[static_cast<std::size_t>(slot)];
                if (v.tapCount > 0)
                    f.flags |= trace::Crossfading;
                if (amounts[op] > 0.0f && v.valid)
                {
                    const double lag = static_cast<double>(now) - v.prevR;
                    f.lagSamples[slot] = std::isfinite(lag) ? static_cast<float>(std::clamp(lag, 0.0, static_cast<double>(capacity))) : 0.0f;
                    f.rate[slot] = std::isfinite(v.prevRate) ? static_cast<float>(v.prevRate) : 1.0f;
                }
            }
            f.gateGain = gateGain;
            f.inPeak = traceInPeak;
            f.outPeak = traceOutPeak;
            traceSink->push(f);
            traceInPeak = 0.0f;
            traceOutPeak = 0.0f;
        }
        ++now;
    }
}
} // namespace groovemorph::core
