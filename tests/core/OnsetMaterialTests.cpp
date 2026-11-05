#include "core/dsp/OnsetDetector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <vector>

namespace
{
constexpr double duration = 8.0;
constexpr int expectedHits = 16;

// Deterministic, synthetic rendered material: a kick/snare-like percussive loop,
// a bass-heavy sustained signal, and a dense sustained chord/noise bed. These
// fixtures exercise retrigger behaviour; they do not substitute for real recordings.
std::vector<float> render(double rate, int material)
{
    std::vector<float> samples(static_cast<std::size_t>(duration * rate));
    std::uint32_t noiseState = 0x142857u;
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const double t = static_cast<double>(i) / rate;
        noiseState = noiseState * 1664525u + 1013904223u;
        const double noise = static_cast<double>(noiseState >> 8) / 8388608.0 - 1.0;
        double value = 0.0;
        if (material == 0 && t >= 0.25)
        {
            const double age = std::fmod(t - 0.25, 0.5);
            value = 0.6 * std::exp(-age / 0.045) * std::sin(2.0 * std::numbers::pi * 90.0 * age)
                  + 0.3 * std::exp(-age / 0.015) * noise;
        }
        else if (material == 1)
            value = 0.35 * std::sin(2.0 * std::numbers::pi * 55.0 * t)
                  + 0.15 * std::sin(2.0 * std::numbers::pi * 110.0 * t);
        else if (material == 2)
        {
            for (const double frequency : {220.0, 277.18, 329.63, 440.0})
                value += 0.12 * std::sin(2.0 * std::numbers::pi * frequency * t);
            value += 0.05 * noise;
        }
        samples[i] = static_cast<float>(value);
    }
    return samples;
}

int countOnsets(const std::vector<float>& samples, double rate, int block, float sensitivity)
{
    groovemorph::core::OnsetDetector detector;
    detector.prepare(rate);
    int count = 0;
    for (std::size_t offset = 0; offset < samples.size(); offset += static_cast<std::size_t>(block))
        count += detector.process(samples.data() + offset,
            static_cast<int>(std::min(samples.size() - offset, static_cast<std::size_t>(block))), sensitivity);
    return count;
}
}

int main()
{
    int failures = 0;
    for (const double rate : {44100.0, 48000.0, 96000.0})
        for (int material = 0; material < 3; ++material)
        {
            const auto samples = render(rate, material);
            for (const float sensitivity : {0.2f, 0.6f, 1.0f})
            {
                const int baseline = countOnsets(samples, rate, 64, sensitivity);
                std::cout << "F07-T09 synthetic material=" << material << " rate=" << rate
                          << " sensitivity=" << sensitivity << " onsets=" << baseline
                          << " retriggersPerSecond=" << baseline / duration << '\n';
                // The current contract gives no maximum-sensitivity false-trigger target.
                // At ordinary sensitivity sustained material should not retrigger; at
                // maximum sensitivity measure it and enforce the 120 ms refractory bound.
                const int refractoryBound = static_cast<int>(std::ceil(duration / 0.120));
                if ((material == 0 && baseline != expectedHits)
                    || (material != 0 && sensitivity <= 0.6f && baseline > 1)
                    || baseline > refractoryBound)
                {
                    std::cerr << "F07-T09 missed/repeated hit or sustained-signal retrigger\n";
                    ++failures;
                }
                for (const int block : {1, 480, 2048})
                    if (countOnsets(samples, rate, block, sensitivity) != baseline)
                    {
                        std::cerr << "F07-T09 block-size-dependent onset count\n";
                        ++failures;
                    }
            }
        }
    return failures == 0 ? 0 : 1;
}
