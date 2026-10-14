#pragma once

#include "core/OperationParams.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

// Pure read-position maths of the five rhythmic stages, shared by RhythmEngine (audio) and the
// visual-feedback planner so both always agree. Moved verbatim out of RhythmEngine.cpp: any change
// here is an audible change and must update EngineGoldenTests deliberately.
namespace groovemorph::core::stage
{
inline constexpr int repeatCycleSlices = 4;

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
        case OperationType::Repeat: return static_cast<double>(repeatCycleSlices - 1);
        case OperationType::ReverseSlice: return 2.0;
        default: return 1.0;
    }
}

inline float sanitiseUnit(float v, float fallback) noexcept
{
    if (!std::isfinite(v))
        return fallback;
    return std::clamp(v, 0.0f, 1.0f);
}

inline double sanitiseSliceBeats(float v) noexcept
{
    if (!std::isfinite(v))
        return 0.25;
    return std::clamp(static_cast<double>(v), 1.0 / 16.0, 4.0);
}

// Samples elapsed since the start of the grid cell of length cellBeats containing beat.
inline double positionInCell(double beat, double cellBeats, double samplesPerBeat, double cellSamples) noexcept
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

// Gate is open for the first half of each slice.
inline bool gateOpen(double beat, double sliceBeats, double samplesPerBeat, double sliceSamples) noexcept
{
    return positionInCell(beat, sliceBeats, samplesPerBeat, sliceSamples) < 0.5 * sliceSamples;
}

struct ReadState
{
    double r = 0.0;    // history read position (samples, engine clock)
    double rate = 1.0; // read-position advance per output sample (negative = reverse)
};

// Read position of a history stage at engine time t. `beat` must already have the operation's phase
// offset subtracted; `len` is the effective slice length in samples. Operations that read no
// history (Gate) and OperationType::Count (no valid timing) read live: {t, 1}.
inline ReadState readPosition(OperationType op, double t, double beat, double sliceBeats, double samplesPerBeat,
                              double len) noexcept
{
    ReadState s{t, 1.0};
    switch (op)
    {
        case OperationType::Repeat:
        {
            const double cyclePos = positionInCell(beat, sliceBeats * repeatCycleSlices, samplesPerBeat,
                                                   len * repeatCycleSlices);
            const double j = std::min(static_cast<double>(repeatCycleSlices - 1), std::floor(cyclePos / len));
            s.r = t - j * len;
            s.rate = 1.0;
            break;
        }
        case OperationType::ReverseSlice:
        {
            const double p = positionInCell(beat, sliceBeats, samplesPerBeat, len);
            s.r = t - 2.0 * p - 1.0;
            s.rate = -1.0;
            break;
        }
        case OperationType::HalfTime:
        {
            const double p = positionInCell(beat, sliceBeats, samplesPerBeat, len);
            s.r = t - 0.5 * p;
            s.rate = 0.5;
            break;
        }
        case OperationType::TapeStop:
        {
            const double p = positionInCell(beat, sliceBeats, samplesPerBeat, len);
            s.r = t - p * p / (2.0 * len);
            s.rate = 1.0 - p / len;
            break;
        }
        default: break;
    }
    return s;
}
} // namespace groovemorph::core::stage
