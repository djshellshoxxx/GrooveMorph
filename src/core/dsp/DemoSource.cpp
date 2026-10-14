#include "core/dsp/DemoSource.h"

#include <cmath>
#include <cstdint>

namespace groovemorph::core
{
namespace
{
constexpr double twoPi = 6.283185307179586;

float noise(std::uint64_t n) noexcept
{
    n ^= n >> 33;
    n *= 0xff51afd7ed558ccdULL;
    n ^= n >> 33;
    n *= 0xc4ceb9fe1a85ec53ULL;
    n ^= n >> 33;
    return static_cast<float>(static_cast<double>(n >> 40) / 8388608.0 - 1.0);
}
} // namespace

void DemoSource::render(float* const* channels, int numChannels, int numSamples, double beatAtStart,
                        double samplesPerBeat, double sampleRate) noexcept
{
    if (channels == nullptr || numSamples <= 0 || !(samplesPerBeat > 0.0) || !(sampleRate > 0.0)
        || !std::isfinite(beatAtStart))
        return;
    const double beatSeconds = samplesPerBeat / sampleRate;
    for (int i = 0; i < numSamples; ++i)
    {
        const double beat = beatAtStart + static_cast<double>(i) / samplesPerBeat;
        const double whole = std::floor(beat);
        const double frac = beat - whole;
        const double t = frac * beatSeconds; // seconds since the beat started
        const auto beatIndex = static_cast<long long>(whole);
        const long long inBar = ((beatIndex % 4) + 4) % 4;
        float v = 0.0f;
        // Kick: pitch-swept sine with exponential decay.
        {
            const double env = std::exp(-t * 22.0);
            const double phase = twoPi * (50.0 * t + 90.0 * (1.0 - std::exp(-t * 30.0)) / 30.0);
            v += static_cast<float>(0.9 * env * std::sin(phase));
        }
        // Snare on 2 and 4: noise burst plus a short tone.
        if (inBar == 1 || inBar == 3)
        {
            const double env = std::exp(-t * 18.0);
            v += static_cast<float>(0.5 * env * noise(static_cast<std::uint64_t>(beatAtStart * 1000.0) * 7919u
                                                      + static_cast<std::uint64_t>(i)));
            v += static_cast<float>(0.25 * env * std::sin(twoPi * 190.0 * t));
        }
        // Hat on the off-beat eighth: short noise burst.
        {
            const double offT = (frac >= 0.5 ? frac - 0.5 : -1.0) * beatSeconds;
            if (offT >= 0.0)
                v += static_cast<float>(0.22 * std::exp(-offT * 90.0)
                                        * noise(static_cast<std::uint64_t>(i) * 31u + 5u
                                                + static_cast<std::uint64_t>(beatIndex) * 977u));
        }
        v *= level;
        for (int c = 0; c < numChannels; ++c)
            channels[c][i] = v;
    }
}
} // namespace groovemorph::core
