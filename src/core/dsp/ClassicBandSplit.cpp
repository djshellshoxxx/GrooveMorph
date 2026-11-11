#include "core/dsp/ClassicBandSplit.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr float svfK = 1.41421356f; // Butterworth damping, single precision as in the original code
} // namespace

void ClassicBandSplit::prepare(double sampleRate, int maxBlockSize)
{
    fs = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
    capacity = std::max(1, maxBlockSize);
    const auto n = static_cast<std::size_t>(capacity);
    for (std::size_t c = 0; c < static_cast<std::size_t>(maxChannels); ++c)
    {
        bypass[c].assign(n, 0.0f);
        part[c].assign(n, 0.0f);
    }
    updateCoefficients();
    reset();
}

void ClassicBandSplit::reset() noexcept
{
    lowState.fill(0.0f);
    svfIc1.fill(0.0f);
    svfIc2.fill(0.0f);
}

void ClassicBandSplit::setMode(int m) noexcept
{
    const int clamped = std::clamp(m, 0, 2);
    if (clamped == bandMode)
        return;
    bandMode = clamped;
    reset();
}

void ClassicBandSplit::setSlope(ClassicSlope s) noexcept
{
    if (s == bandSlope)
        return;
    bandSlope = s;
    reset();
}

void ClassicBandSplit::setCutoff(double hz) noexcept
{
    const double safe = std::isfinite(hz) ? hz : 120.0;
    const double clamped = std::clamp(safe, minCutoffHz, std::min(maxCutoffHz, 0.45 * fs));
    if (clamped == cutoff)
        return;
    cutoff = clamped;
    updateCoefficients();
}

void ClassicBandSplit::updateCoefficients() noexcept
{
    onePoleCoefficient = static_cast<float>(1.0 - std::exp(-2.0 * pi * cutoff / fs));
    const double g = std::tan(pi * cutoff / fs);
    const double k = std::sqrt(2.0);
    const double a1 = 1.0 / (1.0 + g * (g + k));
    svfA1 = static_cast<float>(a1);
    svfA2 = static_cast<float>(g * a1);
    svfA3 = static_cast<float>(g * g * a1);
}

void ClassicBandSplit::split(float* const* channels, int numChannels, int n) noexcept
{
    if (bandMode == 0 || channels == nullptr || capacity <= 0)
        return;
    const int channelCount = std::clamp(numChannels, 0, maxChannels);
    const int count = std::clamp(n, 0, capacity);
    const bool effectedIsHigh = bandMode == 1;
    for (int c = 0; c < channelCount; ++c)
    {
        const auto ci = static_cast<std::size_t>(c);
        float* x = channels[ci];
        if (x == nullptr)
            continue;
        float* bypassOut = bypass[ci].data();
        float* partOut = part[ci].data();
        if (bandSlope == ClassicSlope::Butterworth12)
        {
            float ic1 = svfIc1[ci], ic2 = svfIc2[ci];
            if (!std::isfinite(ic1) || !std::isfinite(ic2))
                ic1 = ic2 = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                const float in = std::isfinite(x[i]) ? x[i] : 0.0f;
                const float v3 = in - ic2;
                const float v1 = svfA1 * ic1 + svfA2 * v3;
                const float v2 = ic2 + svfA2 * ic1 + svfA3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                const float low = v2;
                const float high = in - svfK * v1 - v2;
                const float effected = effectedIsHigh ? high : low;
                x[i] = effected;
                bypassOut[i] = in - effected; // exact complement
                partOut[i] = effected;
            }
            svfIc1[ci] = ic1;
            svfIc2[ci] = ic2;
            continue;
        }
        float state = lowState[ci];
        if (!std::isfinite(state))
            state = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            // A non-finite sample must not poison the filter state for every later block.
            const float in = std::isfinite(x[i]) ? x[i] : 0.0f;
            state += onePoleCoefficient * (in - state);
            const float low = state;
            const float high = in - low;
            if (effectedIsHigh) { bypassOut[i] = low; x[i] = high; }
            else { bypassOut[i] = high; x[i] = low; }
            partOut[i] = x[i];
        }
        lowState[ci] = state;
    }
}

void ClassicBandSplit::merge(float* const* channels, const float* const* originals, int numChannels, int n) noexcept
{
    if (bandMode == 0 || channels == nullptr || originals == nullptr || capacity <= 0)
        return;
    const int channelCount = std::clamp(numChannels, 0, maxChannels);
    const int count = std::clamp(n, 0, capacity);
    for (int c = 0; c < channelCount; ++c)
    {
        const auto ci = static_cast<std::size_t>(c);
        float* x = channels[ci];
        const float* original = originals[ci];
        if (x == nullptr || original == nullptr)
            continue;
        const float* bypassIn = bypass[ci].data();
        const float* partIn = part[ci].data();
        for (int i = 0; i < count; ++i)
            x[i] = x[i] == partIn[i] ? original[i] : x[i] + bypassIn[i]; // untouched band: exact dry
    }
}
} // namespace groovemorph::core::dsp
