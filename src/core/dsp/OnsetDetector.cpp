#include "core/dsp/OnsetDetector.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core
{
namespace
{
float coefficient(double rate, double seconds) noexcept
{
    return static_cast<float>(1.0 - std::exp(-1.0 / std::max(1.0, rate * seconds)));
}
} // namespace

void OnsetDetector::prepare(double sampleRate) noexcept
{
    rate = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
    fastAttack = coefficient(rate, 0.0005);
    fastRelease = coefficient(rate, 0.010);
    slowAttack = coefficient(rate, 0.020);
    slowRelease = coefficient(rate, 0.200);
    refractorySamples = static_cast<int>(rate * 0.120);
    warmupSamples = static_cast<int>(rate * 0.050); // envelopes settle before onsets can fire
    reset();
}

void OnsetDetector::reset() noexcept
{
    fast = slow = 0.0f;
    refractory = 0;
    warmup = warmupSamples;
    armed = false; // must see the signal settle (or fall silent) before the first onset
}

int OnsetDetector::process(const float* samples, int numSamples, float sensitivity) noexcept
{
    if (samples == nullptr || numSamples <= 0)
        return 0;
    const float s = std::isfinite(sensitivity) ? std::clamp(sensitivity, 0.0f, 1.0f) : 0.5f;
    const float ratio = 3.0f - 1.8f * s; // 3.0 (hard to trigger) .. 1.2 (easy)
    constexpr float floorLevel = 0.01f;
    int onsets = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = std::isfinite(samples[i]) ? std::fabs(samples[i]) : 0.0f;
        fast += (x > fast ? fastAttack : fastRelease) * (x - fast);
        slow += (x > slow ? slowAttack : slowRelease) * (x - slow);
        if (fast < 1.0e-20f) fast = 0.0f; // never sit in the subnormal range
        if (slow < 1.0e-20f) slow = 0.0f;
        if (refractory > 0)
            --refractory;
        if (warmup > 0)
        {
            --warmup;
            continue;
        }
        if (!armed && fast < slow * 1.1f + floorLevel * 0.5f)
            armed = true;
        if (armed && refractory == 0 && fast > floorLevel && fast > slow * ratio)
        {
            ++onsets;
            armed = false;
            refractory = refractorySamples;
        }
    }
    return onsets;
}
} // namespace groovemorph::core
