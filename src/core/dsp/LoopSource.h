#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace groovemorph::core
{
// Beat-locked loop playback source (F09 import/capture). The loop covers `loopBeats` meter beats and
// is read from the timeline position, so it follows seeks, loops and tempo changes (pitch follows
// tempo: there is no time stretching in this alpha).
//
// Threading: load() runs on the message thread and fills the idle one of two preallocated buffers,
// then publishes it with a release store; render() runs on the audio thread, reads only the
// published buffer and never allocates or blocks. load() waits (bounded) for the audio thread to
// leave the previously published buffer before reusing it.
class LoopSource final
{
public:
    static constexpr double maxSeconds = 20.0;
    static constexpr int maxChannels = 2;

    // Not real-time safe. Allocates both buffers for the sample rate.
    void prepare(double sampleRate);
    // Message thread. Copies up to maxSeconds of audio (mono is duplicated). The loop spans loopBeats
    // meter beats; originBeats is the timeline beat (modulo loopBeats) at which the loop starts.
    // Returns false for invalid input.
    bool load(const float* const* channels, int numChannels, std::int64_t numSamples, double loopBeats,
              double originBeats);
    void clear() noexcept;
    [[nodiscard]] bool hasLoop() const noexcept { return active.load(std::memory_order_acquire) >= 0; }
    [[nodiscard]] double loopBeats() const noexcept { return beats[0].load(std::memory_order_relaxed); }

    // Audio thread. Overwrites the channels with the loop at the given timeline position.
    // Returns false (buffer untouched) when there is no loop or the timing is invalid.
    bool render(float* const* channels, int numChannels, int numSamples, double beatAtStart,
                double samplesPerBeat) noexcept;

private:
    std::vector<float> data[2];
    std::atomic<std::int64_t> length[2] = {{0}, {0}};
    std::atomic<double> beats[2] = {{0.0}, {0.0}};
    std::atomic<double> origin[2] = {{0.0}, {0.0}};
    std::atomic<int> active{-1};
    std::atomic<std::uint64_t> renderCounter{0};
    std::uint64_t counterAtPublish = 0;
    std::int64_t capacity = 0;
    double rate = 48000.0;
};

// Records the pre-effect signal for an exact number of bars, starting on the next bar line, for
// handing to LoopSource (F09 capture). Audio-thread state machine; the message thread only calls
// arm()/cancel() and, once state() == Done, reads the buffer and calls release().
class LoopCapture final
{
public:
    enum class State : int { Idle = 0, Armed = 1, Recording = 2, Done = 3 };

    void prepare(double sampleRate);
    // Message thread. Records `bars` bars of `beatsPerBar` beats. False if busy or invalid.
    bool arm(int bars, int beatsPerBar);
    void cancel() noexcept;
    [[nodiscard]] State state() const noexcept { return static_cast<State>(stateValue.load(std::memory_order_acquire)); }
    // Valid while state() == Done.
    [[nodiscard]] const float* channel(int c) const noexcept { return data[c < 0 ? 0 : (c > 1 ? 1 : c)].data(); }
    [[nodiscard]] std::int64_t recordedSamples() const noexcept { return recorded.load(std::memory_order_relaxed); }
    [[nodiscard]] double loopBeats() const noexcept { return recordBeats.load(std::memory_order_relaxed); }
    [[nodiscard]] double startBeat() const noexcept { return startBeatValue.load(std::memory_order_relaxed); }
    void release() noexcept { stateValue.store(static_cast<int>(State::Idle), std::memory_order_release); }

    // Audio thread. channels = the signal to record (read only).
    void process(const float* const* channels, int numChannels, int numSamples, double beatAtStart,
                 double samplesPerBeat, bool playing) noexcept;

private:
    std::vector<float> data[2];
    std::int64_t capacity = 0;
    std::atomic<int> stateValue{0};
    std::atomic<int> requestedBars{0};
    std::atomic<int> requestedBeatsPerBar{4};
    std::atomic<std::int64_t> recorded{0};
    std::atomic<double> recordBeats{0.0};
    std::atomic<double> startBeatValue{0.0};
    std::int64_t target = 0;
    std::int64_t written = 0;
};
} // namespace groovemorph::core
