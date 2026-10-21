#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace groovemorph::core
{
// N04 Print to WAV: records the PROCESSED output for an exact number of bars, starting on the next bar
// line, into a bounded stereo buffer that is allocated on the message thread (arm) and freed there
// (release/cancel). The audio thread only calls process()/abortBody(): no allocation, no locks, no I/O.
//
// Unlike LoopCapture (pre-effect input, 20 s) this keeps up to maxBodySeconds of bar-aligned body plus an
// optional tail of up to maxTailSeconds. The tail is simply whatever the plugin outputs after the last
// bar; the timing checks (transport stop, seek, tempo change) only apply to the body.
class OutputCapture final
{
public:
    enum class State : int { Idle = 0, Armed = 1, Recording = 2, Done = 3, Aborted = 4 };
    enum class Failure : int { None = 0, TooLong = 1, TransportStopped = 2, Jumped = 3, TempoChanged = 4 };

    static constexpr double maxBodySeconds = 90.0;
    static constexpr double maxTailSeconds = 8.0;
    static constexpr int maxBars = 16;
    static constexpr int maxBeatsPerBar = 16;

    // Not real-time safe. Sets the session rate and frees any buffers (call with the audio stopped).
    void prepare(double sampleRate);

    // Message thread. Allocates the buffers. False when busy (state != Idle), invalid arguments, or no
    // memory. tailSeconds is clamped to 0..maxTailSeconds.
    bool arm(int bars, int beatsPerBar, double tailSeconds);
    // Message thread. Back to Idle from any state and frees the buffers (bounded wait for a block in flight).
    void cancel() noexcept { release(); }
    void release() noexcept;

    [[nodiscard]] State state() const noexcept { return static_cast<State>(stateValue.load(std::memory_order_acquire)); }
    [[nodiscard]] Failure failure() const noexcept { return static_cast<Failure>(failureReason.load(std::memory_order_acquire)); }
    // 0..1 while Recording (body + tail), 1 when Done.
    [[nodiscard]] double progress() const noexcept;

    // Valid while state() == Done (until release()).
    [[nodiscard]] const float* channel(int c) const noexcept { return data[c <= 0 ? 0 : 1].data(); }
    [[nodiscard]] std::int64_t recordedSamples() const noexcept { return recorded.load(std::memory_order_acquire); }
    [[nodiscard]] std::int64_t bodySamples() const noexcept { return bodyLength.load(std::memory_order_acquire); }
    [[nodiscard]] double loopBeats() const noexcept { return recordBeats.load(std::memory_order_acquire); }
    [[nodiscard]] double startBeat() const noexcept { return startBeatValue.load(std::memory_order_acquire); }
    [[nodiscard]] double startBpm() const noexcept { return startBpmValue.load(std::memory_order_acquire); }
    [[nodiscard]] double sampleRate() const noexcept { return rate; }
    [[nodiscard]] std::int64_t capacitySamples() const noexcept { return capacity; }
    [[nodiscard]] std::int64_t bodyCapacitySamples() const noexcept { return bodyCapacity; }

    // Audio thread. channels = the processed output (read only). beatAtStart/samplesPerBeat describe the
    // block (timeline beats, meter beats); playing = the transport runs and the timing is valid.
    void process(const float* const* channels, int numChannels, int numSamples, double beatAtStart,
                 double samplesPerBeat, bool playing) noexcept;
    // Audio thread. A seek/loop happened: abort a recording that is still in its body.
    void abortBody(Failure why) noexcept;

private:
    void processImpl(const float* const* channels, int numChannels, int numSamples, double beatAtStart,
                     double samplesPerBeat, bool playing) noexcept;
    void fail(Failure why) noexcept;

    std::vector<float> data[2];
    double rate = 48000.0;
    std::int64_t capacity = 0;      // body + tail samples
    std::int64_t bodyCapacity = 0;
    std::int64_t tailCapacity = 0;
    std::atomic<int> stateValue{0};
    std::atomic<int> failureReason{0};
    std::atomic<bool> busy{false};  // audio thread is inside process()
    std::atomic<int> requestedBars{0};
    std::atomic<int> requestedBeatsPerBar{4};
    std::atomic<std::int64_t> requestedTail{0};
    std::atomic<std::int64_t> recorded{0};
    std::atomic<std::int64_t> bodyLength{0};
    std::atomic<std::int64_t> writtenStatus{0};
    std::atomic<std::int64_t> totalStatus{0};
    std::atomic<double> recordBeats{0.0};
    std::atomic<double> startBeatValue{0.0};
    std::atomic<double> startBpmValue{0.0};
    // Audio thread only.
    std::int64_t target = 0;        // body samples
    std::int64_t total = 0;         // body + tail
    std::int64_t written = 0;
    double startSamplesPerBeat = 0.0;
    double expectedBeat = 0.0;
    bool justStarted = false;
};

// Recovers the wet (effect) signal from a processed block: engine output is dry + a * (wet - dry), so
// wet = dry + (out - dry) / a. Needs a >= minWetAmount; below that nothing is changed and false is returned
// (the caller then records the full output). In place on `out`.
inline constexpr float minWetAmount = 0.01f;
bool extractWet(float* out, const float* dry, float amount, int numSamples) noexcept;
} // namespace groovemorph::core
