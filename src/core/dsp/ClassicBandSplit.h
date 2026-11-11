#pragma once

#include <array>
#include <cstddef>
#include <vector>

// Classic complementary band split (docs/specs/F03-GROOVE-PROTECTION.md section 2, `bandTopology` =
// Classic). Portable, zero latency, no allocation after prepare(). The band handed to the effects is
// either a one-pole low-pass / its subtractive complement (6 dB/oct) or a 2nd-order Butterworth TPT
// state-variable output (12 dB/oct); the bypassed band is always `in - effected`, so the two bands
// sum back to the input and any split is bit-exact when the effects change nothing.
// The arithmetic is the single-precision code that lived in PluginProcessor.cpp until the beta,
// moved here unchanged so existing projects sound the same (TODO B-09 / F03-T06).
namespace groovemorph::core::dsp
{
enum class ClassicSlope : int { OnePole6 = 0, Butterworth12 = 1 };

class ClassicBandSplit final
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double minCutoffHz = 20.0;
    static constexpr double maxCutoffHz = 1000.0;

    // Allocates the bypass/part scratch (maxBlockSize samples per channel) and clears the states.
    void prepare(double sampleRate, int maxBlockSize);
    // Clears the filter states only; mode, slope and cutoff are kept.
    void reset() noexcept;

    // mode: 0 = full band (split/merge are no-ops), 1 = Protect lows (effects get the high band),
    // 2 = Lows only (effects get the low band). A change clears the filter states, because a state
    // that stopped advancing while another mode was active would click when it resumes.
    void setMode(int m) noexcept;
    // A slope change clears the filter states for the same reason.
    void setSlope(ClassicSlope s) noexcept;
    // Cutoff in Hz, clamped to 20..1000 Hz and 0.45 fs. Takes effect at the next split().
    void setCutoff(double hz) noexcept;

    [[nodiscard]] int mode() const noexcept { return bandMode; }
    [[nodiscard]] ClassicSlope slope() const noexcept { return bandSlope; }
    [[nodiscard]] double cutoffHz() const noexcept { return cutoff; }
    [[nodiscard]] bool active() const noexcept { return bandMode != 0; }

    // Replaces `channels` with the effected band and keeps the bypassed band and the effected band
    // internally. Non-finite input samples are treated as 0 and never poison the states. Blocks longer
    // than prepare()'s maxBlockSize are processed up to that size. Mode 0: untouched.
    void split(float* const* channels, int numChannels, int n) noexcept;
    // After the engine: adds the bypassed band back. Where the engine left a sample unchanged the
    // caller's original (unsplit) sample is output bit-exactly. `originals` must hold the samples that
    // `channels` held before split(). Mode 0: untouched.
    void merge(float* const* channels, const float* const* originals, int numChannels, int n) noexcept;

    // Read access for tests: the bands produced by the last split().
    [[nodiscard]] const float* bypassBand(int c) const noexcept { return bypass[static_cast<std::size_t>(c)].data(); }
    [[nodiscard]] const float* effectedBand(int c) const noexcept { return part[static_cast<std::size_t>(c)].data(); }

private:
    void updateCoefficients() noexcept;

    double fs = 48000.0;
    double cutoff = 120.0;
    int bandMode = 0;
    ClassicSlope bandSlope = ClassicSlope::OnePole6;
    int capacity = 0;
    // 6 dB/oct one-pole: low += coefficient * (in - low).
    float onePoleCoefficient = 0.0f;
    std::array<float, maxChannels> lowState{};
    // 12 dB/oct Zavalishin/Cytomic TPT state variable filter, Butterworth (k = sqrt 2).
    float svfA1 = 0.0f, svfA2 = 0.0f, svfA3 = 0.0f;
    std::array<float, maxChannels> svfIc1{}, svfIc2{};
    std::array<std::vector<float>, maxChannels> bypass, part;
};
} // namespace groovemorph::core::dsp
