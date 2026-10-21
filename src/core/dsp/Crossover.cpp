#include "core/dsp/Crossover.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int coefficientChunk = 16;
constexpr double sqrt2 = 1.4142135623730951;
} // namespace

void LrChannel::reset() noexcept
{
    s1 = s2 = s3 = Svf{};
    p1 = p2 = p3 = 0.0;
}

void LrChannel::setCutoff(double hz, double sampleRate) noexcept
{
    const double g = std::tan(pi * hz / sampleRate);
    const double a = 1.0 / (1.0 + g * (g + sqrt2));
    a1 = a;
    a2 = g * a;
    a3 = g * g * a;
    g1 = g / (1.0 + g);
}

void LrChannel::process(float inF, float& low, float& high) noexcept
{
    const double in = static_cast<double>(inF);
    if (topology == Topology::LR4)
    {
        // Stage 1 on the input: LP and HP from one shared state.
        const double v3 = in - s1.ic2;
        const double v1 = a1 * s1.ic1 + a2 * v3;
        const double v2 = s1.ic2 + a2 * s1.ic1 + a3 * v3;
        s1.ic1 = 2.0 * v1 - s1.ic1;
        s1.ic2 = 2.0 * v2 - s1.ic2;
        const double lp1 = v2;
        const double hp1 = in - sqrt2 * v1 - v2;
        // Stage 2, low path.
        const double w3 = lp1 - s2.ic2;
        const double w1 = a1 * s2.ic1 + a2 * w3;
        const double w2 = s2.ic2 + a2 * s2.ic1 + a3 * w3;
        s2.ic1 = 2.0 * w1 - s2.ic1;
        s2.ic2 = 2.0 * w2 - s2.ic2;
        low = static_cast<float>(w2);
        // Stage 2, high path.
        const double u3 = hp1 - s3.ic2;
        const double u1 = a1 * s3.ic1 + a2 * u3;
        const double u2 = s3.ic2 + a2 * s3.ic1 + a3 * u3;
        s3.ic1 = 2.0 * u1 - s3.ic1;
        s3.ic2 = 2.0 * u2 - s3.ic2;
        high = static_cast<float>(hp1 - sqrt2 * u1 - u2);
        return;
    }
    // LR2: cascaded 1-pole TPT sections; the high band is returned inverted so low + high is flat.
    const double v = (in - p1) * g1;
    const double lp1 = v + p1;
    p1 = lp1 + v;
    const double hp1 = in - lp1;
    const double vl = (lp1 - p2) * g1;
    const double lp2 = vl + p2;
    p2 = lp2 + vl;
    low = static_cast<float>(lp2);
    const double vh = (hp1 - p3) * g1;
    const double lph = vh + p3;
    p3 = lph + vh;
    high = static_cast<float>(-(hp1 - lph));
}

void BandSplit::prepare(double sampleRate, int maxBlockSize)
{
    fs = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
    const auto n = static_cast<std::size_t>(std::max(1, maxBlockSize));
    for (std::size_t c = 0; c < static_cast<std::size_t>(maxChannels); ++c)
    {
        orig[c].assign(n, 0.0f);
        bypass[c].assign(n, 0.0f);
        part[c].assign(n, 0.0f);
    }
    glide = static_cast<float>(1.0 - std::exp(-static_cast<double>(coefficientChunk) / (0.005 * fs)));
    riseStep = static_cast<float>(1.0 / std::max(1.0, 0.003 * fs));
    fallStep = static_cast<float>(1.0 / std::max(1.0, 0.010 * fs));
    holdSamples = std::max(1, static_cast<int>(std::lround(0.010 * fs)));
    reset();
}

void BandSplit::reset() noexcept
{
    for (auto& f : filters)
    {
        f.reset();
        f.setTopology(topology);
    }
    working = target;
    appliedHz = 0.0;
    updateCoefficients();
    blend = 0.0f;
    holdLeft = 0;
    untilCoefficient = coefficientChunk;
}

void BandSplit::setTopology(Topology t) noexcept
{
    if (t == topology)
        return;
    topology = t;
    reset();
}

void BandSplit::setMode(int m) noexcept
{
    m = std::clamp(m, 1, 2);
    if (m == mode)
        return;
    mode = m;
    reset();
}

void BandSplit::setCutoff(double hz) noexcept
{
    if (!std::isfinite(hz))
        hz = 120.0;
    target = std::clamp(hz, 20.0, std::min(1000.0, 0.45 * fs));
}

void BandSplit::updateCoefficients() noexcept
{
    if (appliedHz == working)
        return;
    appliedHz = working;
    for (auto& f : filters)
        f.setCutoff(working, fs);
}

void BandSplit::split(float* const* channels, int numChannels, int n) noexcept
{
    const int nch = std::min(numChannels, maxChannels);
    if (orig[0].empty() || n <= 0)
        return;
    n = std::min<int>(n, static_cast<int>(orig[0].size()));
    for (int i = 0; i < n; ++i)
    {
        if (untilCoefficient <= 0)
        {
            untilCoefficient = coefficientChunk;
            if (working != target)
            {
                working += (target - working) * static_cast<double>(glide);
                if (std::abs(target - working) < 1.0e-4 * target)
                    working = target;
                updateCoefficients();
            }
        }
        --untilCoefficient;
        for (int c = 0; c < nch; ++c)
        {
            const auto ci = static_cast<std::size_t>(c);
            const float raw = channels[c][i];
            const float in = std::isfinite(raw) ? raw : 0.0f;
            float low = 0.0f, high = 0.0f;
            filters[ci].process(in, low, high);
            const float effected = mode == 1 ? high : low;
            const float other = mode == 1 ? low : high;
            orig[ci][static_cast<std::size_t>(i)] = in;
            bypass[ci][static_cast<std::size_t>(i)] = other;
            part[ci][static_cast<std::size_t>(i)] = effected;
            channels[c][i] = effected;
        }
    }
    for (int c = 0; c < nch; ++c) // a poisoned state would persist; the input is already sanitized, so this is a guard
    {
        auto& f = filters[static_cast<std::size_t>(c)];
        if (!std::isfinite(f.s1.ic1 + f.s1.ic2 + f.s2.ic1 + f.s2.ic2 + f.s3.ic1 + f.s3.ic2 + f.p1 + f.p2 + f.p3))
            f.reset();
    }
}

void BandSplit::merge(float* const* channels, int numChannels, int n) noexcept
{
    const int nch = std::min(numChannels, maxChannels);
    if (orig[0].empty() || n <= 0)
        return;
    n = std::min<int>(n, static_cast<int>(orig[0].size()));
    for (int i = 0; i < n; ++i)
    {
        const auto si = static_cast<std::size_t>(i);
        bool changed = false;
        for (int c = 0; c < nch; ++c)
            changed = changed || channels[c][i] != part[static_cast<std::size_t>(c)][si];
        if (changed)
        {
            blend = std::min(1.0f, blend + riseStep);
            holdLeft = holdSamples;
        }
        else if (holdLeft > 0)
            --holdLeft;
        else
            blend = std::max(0.0f, blend - fallStep);
        for (int c = 0; c < nch; ++c)
        {
            const auto ci = static_cast<std::size_t>(c);
            const float o = orig[ci][si];
            if (blend <= 0.0f)
                channels[c][i] = o;
            else
            {
                const float sum = channels[c][i] + bypass[ci][si];
                channels[c][i] = blend >= 1.0f ? sum : o + blend * (sum - o);
            }
        }
    }
}
} // namespace groovemorph::core::dsp
