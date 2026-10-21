#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Pure maths of the two tonal stages FilterSweep and BitCrush (docs/specs/N01-EFFECTS-SWEEP-CRUSH.md).
// Shared by RhythmEngine (audio) and the tests. Portable: standard library only, no allocation.
namespace groovemorph::core::stage
{
inline constexpr double pi = 3.14159265358979323846;

// Position of an operation's slice grid at `beat`: slice index and samples since the slice start.
// Consistent with positionInCell (same snapping), but also tells which slice the sample is in.
struct SliceCursor
{
    std::int64_t index = 0;
    double pos = 0.0;
};

inline SliceCursor sliceCursor(double beat, double sliceBeats, double samplesPerBeat, double sliceSamples) noexcept
{
    SliceCursor c;
    const double index = std::floor(beat / sliceBeats);
    double pos = (beat - index * sliceBeats) * samplesPerBeat;
    const double rounded = std::nearbyint(pos);
    if (std::abs(pos - rounded) < 1.0e-6)
        pos = rounded;
    c.index = static_cast<std::int64_t>(index);
    if (pos >= sliceSamples)
    {
        pos = 0.0;
        ++c.index;
    }
    c.pos = std::max(0.0, pos);
    return c;
}

// ---- FilterSweep -------------------------------------------------------------------------------------------
inline constexpr double sweepQ = 2.5;
inline constexpr double sweepHighHz = 20000.0;
inline constexpr double sweepLowHz = 200.0;

// Cutoff in Hz for sweep position p in [0,1] of a slice. Even slices close (high to low), odd slices open.
inline double sweepCutoffHz(double p, bool opening, double sampleRate) noexcept
{
    const double high = std::min(sweepHighHz, 0.45 * sampleRate);
    const double low = std::min(sweepLowHz, 0.5 * high);
    const double q = std::clamp(opening ? 1.0 - p : p, 0.0, 1.0);
    return high * std::pow(low / high, q);
}

// TPT state-variable filter coefficients (Zavalishin), low-pass output.
struct SvfCoeffs
{
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;
};

inline SvfCoeffs svfCoeffs(double cutoffHz, double sampleRate) noexcept
{
    const double fc = std::clamp(cutoffHz, 1.0, 0.45 * sampleRate);
    const double g = std::tan(pi * fc / sampleRate);
    const double k = 1.0 / sweepQ;
    SvfCoeffs c;
    c.a1 = 1.0 / (1.0 + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

struct SvfState
{
    double ic1 = 0.0, ic2 = 0.0;
};

// Low-pass of one sample. Non-finite results reset the state and pass the input; tiny state values flush to 0.
inline float svfLowPass(SvfState& s, const SvfCoeffs& c, float input) noexcept
{
    const double x = static_cast<double>(input);
    const double v3 = x - s.ic2;
    const double v1 = c.a1 * s.ic1 + c.a2 * v3;
    const double v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
    s.ic1 = 2.0 * v1 - s.ic1;
    s.ic2 = 2.0 * v2 - s.ic2;
    if (std::abs(s.ic1) < 1.0e-25) s.ic1 = 0.0;
    if (std::abs(s.ic2) < 1.0e-25) s.ic2 = 0.0;
    const float out = static_cast<float>(v2);
    if (!std::isfinite(out) || !std::isfinite(s.ic1) || !std::isfinite(s.ic2))
    {
        s = SvfState{};
        return input;
    }
    return out;
}

// Idle tracking: state follows the input so activation starts from steady state (no click).
inline void svfTrack(SvfState& s, float input) noexcept
{
    s.ic1 = 0.0;
    s.ic2 = static_cast<double>(input);
}

// ---- BitCrush ----------------------------------------------------------------------------------------------
inline constexpr double crushStartBits = 16.0;
inline constexpr double crushBitSpan = 12.0;
inline constexpr double crushStartHz = 12000.0;
inline constexpr double crushOctaves = 4.0;

inline double crushBits(double e) noexcept { return crushStartBits - crushBitSpan * std::clamp(e, 0.0, 1.0); }
inline double crushRateHz(double e) noexcept { return crushStartHz * std::exp2(-crushOctaves * std::clamp(e, 0.0, 1.0)); }
// Hold length in samples (>= 1; 1 = no sample-rate reduction).
inline double crushHold(double e, double sampleRate) noexcept { return std::max(1.0, sampleRate / crushRateHz(e)); }

inline float crushQuantise(float x, double bits) noexcept
{
    const double levels = std::exp2(bits - 1.0);
    return static_cast<float>(std::nearbyint(static_cast<double>(x) * levels) / levels);
}

// Shared sample-and-hold counter: returns true when the held sample must be refreshed this sample.
struct CrushClock
{
    double counter = 1.0e9; // large: the first sample after a reset always captures

    bool tick(double hold) noexcept
    {
        counter += 1.0;
        if (counter >= hold)
        {
            counter -= hold;
            if (counter >= hold) // hold shrank a lot since the last capture
                counter = 0.0;
            return true;
        }
        return false;
    }
};
} // namespace groovemorph::core::stage
