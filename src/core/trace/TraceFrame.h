#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Audio-thread telemetry for the Visual window (docs/visual-feedback/SPEC.md sections 2 and 3).
// Portable: standard library only.
namespace groovemorph::core::trace
{
inline constexpr std::int64_t hopSamples = 128; // one frame per 128 samples of the absolute engine clock
inline constexpr std::size_t ringCapacity = 8192; // power of two, about 21.8 s at 48 kHz
inline constexpr std::size_t historyStages = 4;   // slots: Repeat, ReverseSlice, HalfTime, TapeStop
inline constexpr std::size_t stageCount = 7;      // OperationType order (FilterSweep and BitCrush have no history slot)

enum FrameFlags : std::uint32_t
{
    TimingOk = 1u << 0,    // transport playing and timing valid
    Crossfading = 1u << 1, // any history voice has a live crossfade tap
};

struct TraceFrame
{
    std::int64_t sample = 0;   // engine clock of the last sample of the hop window
    std::uint32_t epoch = 0;   // increments on reset/resetHistory; older epochs are discarded by readers
    std::uint32_t flags = 0;   // FrameFlags
    double beat = 0.0;         // meter-beat position of `sample` (0 when not TimingOk)
    float samplesPerBeat = 0.0f; // 0 when not TimingOk
    float intensity = 0.0f;    // smoothed master intensity 0..1
    float amount[stageCount] = {};      // smoothed applied depth per operation
    float lagSamples[historyStages] = {}; // now - read position per history voice; 0 when inactive
    float rate[historyStages] = {1.0f, 1.0f, 1.0f, 1.0f}; // read rate; 1 when inactive
    float gateGain = 1.0f;     // measured gate gain 0..1
    float inPeak = 0.0f;       // max |sample| over channels and the hop window
    float outPeak = 0.0f;
};

static_assert(std::is_trivially_copyable_v<TraceFrame>);
static_assert(sizeof(TraceFrame) == 104, "TraceFrame layout is part of the ring protocol");
static_assert(sizeof(TraceFrame) % sizeof(std::uint64_t) == 0);
} // namespace groovemorph::core::trace
