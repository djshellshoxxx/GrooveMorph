#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace groovemorph::core
{
// Pitch-preserving, non-real-time time stretch (WSOLA: waveform-similarity overlap-add) for loops.
// NEVER call from the audio thread: it allocates and takes tens to hundreds of milliseconds.
//
// ratio = output length / input length. 0.5 .. 2.0 (>1 is slower/longer, pitch unchanged).
// Known artifacts (WSOLA): transients can be doubled or dropped when a frame boundary falls near one
// (slowing down repeats material, speeding up skips it); dense polyphonic material gets a slight
// phasiness/flutter; very low tones (below ~50 Hz) are not tracked by the similarity search.
struct StretchOptions
{
    double ratio = 1.0;
    double sampleRate = 48000.0;
    // Treat the input as one period of a loop: read it circularly and cross-fade the wrap point.
    bool circular = false;
};

namespace TimeStretcher
{
inline constexpr double minRatio = 0.5;
inline constexpr double maxRatio = 2.0;

// Exact output length for an input length and ratio.
[[nodiscard]] std::int64_t outputLength(std::int64_t inputSamples, double ratio) noexcept;

// Stretches `numChannels` (1..8) channels of `numSamples` (>= 16). `out` is resized to numChannels
// vectors of outputLength() samples. Returns false (out unspecified) for invalid input, a ratio outside
// minRatio..maxRatio, a non-finite sample rate, or when `cancel` becomes true.
// Non-finite input samples are treated as 0.
bool stretch(const float* const* in, int numChannels, std::int64_t numSamples, const StretchOptions& options,
             std::vector<std::vector<float>>& out, const std::atomic<bool>* cancel = nullptr);
} // namespace TimeStretcher

// Pure decision function for "stretch to host tempo".
struct StretchPlan
{
    double ratio = 1.0;     // ratio to render (clamped)
    bool limited = false;   // the wanted ratio was out of range / over capacity, remainder stays varispeed
    bool needsNew = false;  // differs from ratioInUse by more than 0.2 %
};
// nativeBpm: tempo at which the loop plays at its original speed; hostBpm: current tempo;
// ratioInUse: ratio of the buffer currently published or being rendered (1 = unstretched);
// maxRatioForCapacity: capacity samples / input samples (<= 0 means no extra limit).
[[nodiscard]] StretchPlan planStretch(double nativeBpm, double hostBpm, double ratioInUse,
                                      double maxRatioForCapacity) noexcept;

// Runs stretch jobs on one background thread. submit() supersedes any queued job and cancels the running
// one; tryTake() hands back the newest finished result without blocking.
class StretchWorker final
{
public:
    struct Job
    {
        std::uint64_t id = 0;
        std::vector<std::vector<float>> input; // one vector per channel, equal lengths
        StretchOptions options;
    };
    struct Result
    {
        std::uint64_t id = 0;
        bool ok = false;
        double ratio = 1.0;
        std::vector<std::vector<float>> output;
    };

    StretchWorker();
    ~StretchWorker();
    StretchWorker(const StretchWorker&) = delete;
    StretchWorker& operator=(const StretchWorker&) = delete;

    void submit(Job&& job);
    // Drops the queued job, cancels the running one and discards any finished result.
    void cancelAll();
    bool tryTake(Result& result);
    [[nodiscard]] bool busy() const;

private:
    void run();

    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    bool hasPending = false;
    bool running = false;
    bool hasResult = false;
    Job pending;
    Result finished;
    std::atomic<bool> cancelRunning{false};
    std::thread thread; // last member: started after everything else is constructed
};
} // namespace groovemorph::core
