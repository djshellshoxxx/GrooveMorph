#pragma once

#include "core/OperationParams.h"
#include "core/dsp/StageGeometry.h"
#include "core/trace/TraceRing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace groovemorph::core
{
// Musical timing for one audio block (GM-FR-006 resolved context).
struct RhythmTiming
{
    double samplesPerBeat = 0.0;   // samples per meter beat; <= 0 or non-finite = no timing
    double beatAtBlockStart = 0.0; // meter-beat position of the first sample in the block
    bool isPlaying = false;
    // Optional per-operation beat-grid origin (meter beats). The operation's slice grid is
    // measured from this value instead of the shared grid; all zeros = legacy shared grid.
    std::array<double, operationCount> phaseOffsetBeats{};
};

// Real-time-safe rhythmic transformation engine (GM-FR-003, GM-FR-005, GM-QR-001,
// GM-QR-003, GM-QR-004, GM-QR-006, GM-QR-007).
//
// Operations chain in OperationType order; each stage output is
//   x + amount * (opOutput - x), amount = enabled ? depth : 0 (ramped over crossfadeSeconds).
// The block output is dry + intensity * (chain - dry); intensity == 0 returns the input
// bit-exactly. Only past audio is read, so the engine adds zero latency.
//
// Slice policy (all slices are aligned to the beat grid: slice k starts at beat k * sliceBeats):
//  - Repeat:       cycles of repeatCycleSlices slices; slice 0 of a cycle plays live and is
//                  then looped for the remaining slices of the cycle.
//  - ReverseSlice: during slice k, slice k-1 is played backwards.
//  - Gate:         open for the first half of each slice, closed for the second half,
//                  with gateRampSeconds linear ramps.
//  - HalfTime:     reads at half speed from each slice start (resync every slice).
//  - TapeStop:     read rate ramps linearly from 1 to 0 across the slice, then resyncs.
// Any discontinuity in an operation's read position (slice wrap, slice-length or variation
// change, transport jump) is crossfaded over crossfadeSeconds (up to two overlapping fades;
// a third discontinuity inside one fade time drops the oldest).
//
// Memory (GM-QR-003): 4 history rings (Repeat, Reverse, HalfTime, TapeStop) per channel, each
// ceil(maxHistorySeconds * sampleRate) + maxBlockSize + 8 floats, allocated in prepare() only
// (about 24.6 MB for stereo at 192 kHz). History-reading slices are clamped so their reads
// stay inside the ring: Repeat <= history/3, ReverseSlice <= history/2, HalfTime and
// TapeStop <= history. Gate is not clamped.
//
// When not playing, or when timing is invalid, all operations ramp to bypass.
class RhythmEngine final
{
public:
    static constexpr double maxHistorySeconds = 4.0;
    static constexpr double crossfadeSeconds = 0.003;
    static constexpr double gateRampSeconds = 0.002;
    static constexpr int repeatCycleSlices = stage::repeatCycleSlices;

    // Allocates all state. Not real-time safe. Resets the engine.
    void prepare(double sampleRate, int maxBlockSize, int maxChannels);

    // Lifecycle reset clears the complete preallocated history and smoothing state.
    // Use resetHistory() for constant-time invalidation inside the callback.
    void reset() noexcept;

    // Discard captured audio after a transport seek without clearing/allocating the rings.
    // Negative history times are treated as silence until fresh audio fills each window.
    // Preserve parameter ramps so a timeline jump does not restart automation smoothing.
    void resetHistory() noexcept;
    // Starts a new trace epoch without touching audio state (the trace beat axis jumped, e.g. a C04 phase shift).
    void startTraceEpoch() noexcept { ++traceEpoch; }
    // After resetHistory() each history operation passes the dry signal until the fresh history covers
    // its read delay (then it crossfades in), instead of replaying silence from an emptied buffer.

    // Processes in place. Channels beyond maxChannels are left untouched. Does nothing
    // before prepare().
    void process(float* const* channels,
                 int numChannels,
                 int numSamples,
                 const VariationSettings& variation,
                 float intensity,
                 const RhythmTiming& timing) noexcept;

    // Optional read-only telemetry for the Visual window (docs/visual-feedback/SPEC.md section 4).
    // nullptr (default) or an inactive ring costs nothing and never changes the audio output. Must
    // not be changed concurrently with process(); the sink must outlive its use by the engine.
    void setTraceSink(trace::TraceRing* sink) noexcept { traceSink = sink; }

    [[nodiscard]] bool isPrepared() const noexcept { return capacity > 0; }
    [[nodiscard]] int crossfadeSamples() const noexcept { return fadeSamples; }
    [[nodiscard]] int gateRampSamples() const noexcept { return gateRamp; }
    [[nodiscard]] std::size_t historyCapacitySamples() const noexcept { return capacity; }
    // Longest effective slice for the operation in samples (0 before prepare()).
    [[nodiscard]] double maxSliceSamples(OperationType type) const noexcept;

private:
    static constexpr std::size_t historyStageCount = 4;

    struct Voice
    {
        double prevR = 0.0;
        double prevRate = 0.0;
        struct Tap
        {
            double r = 0.0;
            double rate = 0.0;
            int remaining = 0; // samples left of the fade out of this tap
        };
        // Fading-out read positions, oldest first. A discontinuity during a running fade
        // stacks a second tap so overlapping fades stay continuous.
        std::array<Tap, 2> taps{};
        int tapCount = 0;
        bool valid = false;
    };

    [[nodiscard]] float readRing(const float* ring, std::int64_t now, double r) const noexcept;

    std::vector<float> rings;
    std::size_t capacity = 0;
    std::size_t historySamples = 0;
    int channelCount = 0;
    int fadeSamples = 1;
    int gateRamp = 1;

    std::int64_t now = 0;
    bool initialised = false;
    float smoothedIntensity = 0.0f;
    std::array<float, operationCount> amounts{};
    std::array<Voice, historyStageCount> voices{};
    float gateGain = 1.0f;
    bool warmingAfterReset = false; // set by resetHistory(), cleared by reset()

    trace::TraceRing* traceSink = nullptr;
    std::uint32_t traceEpoch = 0; // increments on reset()/resetHistory()
    float traceInPeak = 0.0f;     // peak accumulators over the current hop window
    float traceOutPeak = 0.0f;
    bool traceLastTimingOk = true; // a stop/start of timing starts a new epoch (beat is 0 while stopped)
};
} // namespace groovemorph::core
