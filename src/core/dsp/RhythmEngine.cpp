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

// History-ring slot of an operation, or -1 when it reads no history.
constexpr int historySlot(std::size_t op) noexcept
{
    switch (static_cast<OperationType>(op))
    {
        case OperationType::Repeat: return 0;
        case OperationType::ReverseSlice: return 1;
        case OperationType::HalfTime: return 2;
        case OperationType::TapeStop: return 3;
        default: return -1;
    }
}

// Maximum read delay of an operation as a multiple of its slice length.
constexpr double delayMultiple(OperationType type) noexcept
{
    switch (type)
    {
        case OperationType::Repeat: return static_cast<double>(RhythmEngine::repeatCycleSlices - 1);
        case OperationType::ReverseSlice: return 2.0;
        default: return 1.0;
    }
}

float sanitiseUnit(float v, float fallback) noexcept
{
    if (!std::isfinite(v))
        return fallback;
    return std::clamp(v, 0.0f, 1.0f);
}

double sanitiseSliceBeats(float v) noexcept
{
    if (!std::isfinite(v))
        return 0.25;
    return std::clamp(static_cast<double>(v), 1.0 / 16.0, 4.0);
}

float slew(float current, float target, float step) noexcept
{
    if (current < target)
        return std::min(target, current + step);
    if (current > target)
        return std::max(target, current - step);
    return target;
}

// Samples elapsed since the start of the grid cell of length cellBeats containing beat.
double positionInCell(double beat, double cellBeats, double samplesPerBeat, double cellSamples) noexcept
{
    const double index = std::floor(beat / cellBeats);
    double pos = (beat - index * cellBeats) * samplesPerBeat;
    const double rounded = std::nearbyint(pos);
    if (std::abs(pos - rounded) < 1.0e-6)
        pos = rounded;
    if (pos >= cellSamples)
        pos = 0.0;
    return std::max(0.0, pos);
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
}

void RhythmEngine::resetHistory() noexcept
{
    now = 0;
    voices.fill(Voice{});
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
            double samples = beats * spb;
            if (historySlot(op) >= 0)
            {
                samples = std::min(samples, maxSliceSamples(static_cast<OperationType>(op)));
                beats = samples / spb;
            }
            sliceSamples[op] = samples;
        }
        sliceBeats[op] = beats;
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
                const double pos = positionInCell(beat, sliceBeats[g], spb, sliceSamples[g]);
                const float target = pos < 0.5 * sliceSamples[g] ? 1.0f : 0.0f;
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
            double r = t;
            double rate = 1.0;
            switch (timingOk ? static_cast<OperationType>(op) : OperationType::Count)
            {
                case OperationType::Repeat:
                {
                    const double cyclePos = positionInCell(
                        beat, sliceBeats[op] * repeatCycleSlices, spb, len * repeatCycleSlices);
                    const double j = std::min(static_cast<double>(repeatCycleSlices - 1),
                                              std::floor(cyclePos / len));
                    r = t - j * len;
                    rate = 1.0;
                    break;
                }
                case OperationType::ReverseSlice:
                {
                    const double p = positionInCell(beat, sliceBeats[op], spb, len);
                    r = t - 2.0 * p - 1.0;
                    rate = -1.0;
                    break;
                }
                case OperationType::HalfTime:
                {
                    const double p = positionInCell(beat, sliceBeats[op], spb, len);
                    r = t - 0.5 * p;
                    rate = 0.5;
                    break;
                }
                case OperationType::TapeStop:
                {
                    const double p = positionInCell(beat, sliceBeats[op], spb, len);
                    r = t - p * p / (2.0 * len);
                    rate = 1.0 - p / len;
                    break;
                }
                default: break;
            }
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
            const float dry = data[n];
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
            if (smoothedIntensity > 0.0f)
            {
                y = dry + smoothedIntensity * (x - dry);
                if (!std::isfinite(y) || std::abs(y) < 1.0e-30f)
                    y = 0.0f;
            }
            data[n] = y;
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
        ++now;
    }
}
} // namespace groovemorph::core
