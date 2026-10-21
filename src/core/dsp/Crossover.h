#pragma once

#include <array>
#include <cstddef>
#include <vector>

// Linkwitz-Riley band split (docs/specs/W3-LR-CROSSOVER.md). Portable, zero latency, no allocation
// after prepare(). LR2 = two cascaded 1-pole sections per side (high band polarity-inverted so that
// low + high is a 1st-order all-pass); LR4 = two cascaded 2nd-order Butterworth TPT filters per side.
namespace groovemorph::core::dsp
{
enum class Topology : int { LR2 = 0, LR4 = 1 };

// One channel of a Linkwitz-Riley split. low/high are written per sample.
class LrChannel final
{
public:
    void reset() noexcept;
    void setTopology(Topology t) noexcept { topology = t; }
    // Sets filter coefficients for a cutoff in Hz at the given rate.
    void setCutoff(double hz, double sampleRate) noexcept;
    void process(float in, float& low, float& high) noexcept;

private:
    struct Svf
    {
        double ic1 = 0.0, ic2 = 0.0;
    };
    Topology topology = Topology::LR4;
    // 2nd-order TPT (Butterworth, k = sqrt 2).
    double a1 = 0.0, a2 = 0.0, a3 = 0.0;
    // 1-pole TPT.
    double g1 = 0.0; // g / (1 + g)
    Svf s1{}, s2{}, s3{};      // LR4: stage-1 (shared LP/HP source), stage 2 LP, stage 2 HP
    double p1 = 0.0, p2 = 0.0, p3 = 0.0; // LR2 one-pole states: stage 1 (LP), 2 LP, 2 HP
    friend class BandSplit;
};

// Multi-channel splitter with a smoothed cutoff and the "unchanged band is exact dry" merge.
class BandSplit final
{
public:
    static constexpr int maxChannels = 2;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void setTopology(Topology t) noexcept;
    // Target cutoff (clamped to 20..1000 Hz and 0.45 fs). The working cutoff glides (5 ms one-pole).
    void setCutoff(double hz) noexcept;
    // mode: 1 = Protect lows (effects get the high band), 2 = Lows only (effects get the low band).
    void setMode(int m) noexcept;

    // Replaces `channels` with the effected band; keeps the original and the bypassed band. Non-finite
    // input samples are treated as 0.
    void split(float* const* channels, int numChannels, int n) noexcept;
    // After the engine: recombines. Where the engine left the band unchanged the original is output
    // (cross-faded, see the spec).
    void merge(float* const* channels, int numChannels, int n) noexcept;

    [[nodiscard]] const float* original(int c) const noexcept { return orig[static_cast<std::size_t>(c)].data(); }

private:
    void updateCoefficients() noexcept;

    double fs = 48000.0;
    double target = 120.0, working = 120.0, appliedHz = 0.0;
    float glide = 0.0f;
    int mode = 1;
    Topology topology = Topology::LR4;
    std::array<LrChannel, maxChannels> filters{};
    std::array<std::vector<float>, maxChannels> orig, bypass, part;
    float blend = 0.0f;       // 0 = exact original, 1 = recombined bands
    int holdLeft = 0;
    float riseStep = 0.0f, fallStep = 0.0f;
    int holdSamples = 0;
    int untilCoefficient = 0;
};
} // namespace groovemorph::core::dsp
