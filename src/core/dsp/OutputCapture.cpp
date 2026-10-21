#include "core/dsp/OutputCapture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace groovemorph::core
{
namespace
{
constexpr double kBarTolerance = 1.0e-9;
constexpr double kStartRoundingSamples = 0.5001; // a bar line up to half a sample ago still starts here
constexpr double kTempoTolerance = 1.0e-6;

bool finitePositive(double v) noexcept { return std::isfinite(v) && v > 0.0; }
} // namespace

void OutputCapture::prepare(double sampleRate)
{
    rate = finitePositive(sampleRate) ? sampleRate : 48000.0;
    bodyCapacity = static_cast<std::int64_t>(std::ceil(maxBodySeconds * rate));
    tailCapacity = static_cast<std::int64_t>(std::ceil(maxTailSeconds * rate));
    capacity = bodyCapacity + tailCapacity;
    release();
}

void OutputCapture::release() noexcept
{
    stateValue.store(static_cast<int>(State::Idle), std::memory_order_seq_cst);
    // The audio thread sets `busy` before it reads the state, so once busy is false after the state became
    // Idle no block can still touch the buffers. Bounded: a stuck audio thread leaves the memory allocated.
    for (int i = 0; i < 400 && busy.load(std::memory_order_seq_cst); ++i)
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    if (busy.load(std::memory_order_seq_cst))
        return;
    for (auto& d : data)
    {
        std::vector<float> empty;
        d.swap(empty);
    }
}

bool OutputCapture::arm(int bars, int beatsPerBar, double tailSeconds)
{
    if (capacity <= 0 || bars < 1 || bars > maxBars || beatsPerBar < 1 || beatsPerBar > maxBeatsPerBar
        || !std::isfinite(tailSeconds) || tailSeconds < 0.0 || tailSeconds > maxTailSeconds
        || state() != State::Idle)
        return false;
    try
    {
        for (auto& d : data)
            if (d.size() != static_cast<std::size_t>(capacity))
                d.assign(static_cast<std::size_t>(capacity), 0.0f); // Idle: the audio thread never reads
    }
    catch (...)
    {
        for (auto& d : data)
        {
            std::vector<float> empty;
            d.swap(empty);
        }
        return false;
    }
    failureReason.store(0, std::memory_order_seq_cst);
    recorded.store(0, std::memory_order_relaxed);
    bodyLength.store(0, std::memory_order_relaxed);
    writtenStatus.store(0, std::memory_order_relaxed);
    totalStatus.store(0, std::memory_order_relaxed);
    requestedBars.store(bars, std::memory_order_relaxed);
    requestedBeatsPerBar.store(beatsPerBar, std::memory_order_relaxed);
    requestedTail.store(std::min<std::int64_t>(tailCapacity, std::llround(tailSeconds * rate)), std::memory_order_relaxed);
    stateValue.store(static_cast<int>(State::Armed), std::memory_order_seq_cst);
    return true;
}

double OutputCapture::progress() const noexcept
{
    const auto s = state();
    if (s == State::Done)
        return 1.0;
    const auto t = totalStatus.load(std::memory_order_relaxed);
    if (s != State::Recording || t <= 0)
        return 0.0;
    return std::clamp(static_cast<double>(writtenStatus.load(std::memory_order_relaxed)) / static_cast<double>(t), 0.0, 1.0);
}

void OutputCapture::fail(Failure why) noexcept
{
    failureReason.store(static_cast<int>(why), std::memory_order_seq_cst);
    int expected = static_cast<int>(State::Recording);
    if (!stateValue.compare_exchange_strong(expected, static_cast<int>(State::Aborted), std::memory_order_seq_cst))
    {
        expected = static_cast<int>(State::Armed);
        stateValue.compare_exchange_strong(expected, static_cast<int>(State::Aborted), std::memory_order_seq_cst);
    }
}

void OutputCapture::abortBody(Failure why) noexcept
{
    busy.store(true, std::memory_order_seq_cst);
    if (stateValue.load(std::memory_order_seq_cst) == static_cast<int>(State::Recording) && written < target)
        fail(why);
    busy.store(false, std::memory_order_seq_cst);
}

void OutputCapture::process(const float* const* channels, int numChannels, int numSamples, double beatAtStart,
                            double samplesPerBeat, bool playing) noexcept
{
    busy.store(true, std::memory_order_seq_cst);
    processImpl(channels, numChannels, numSamples, beatAtStart, samplesPerBeat, playing);
    busy.store(false, std::memory_order_seq_cst);
}

void OutputCapture::processImpl(const float* const* channels, int numChannels, int numSamples, double beatAtStart,
                                double samplesPerBeat, bool playing) noexcept
{
    const int current = stateValue.load(std::memory_order_seq_cst);
    if ((current != static_cast<int>(State::Armed) && current != static_cast<int>(State::Recording))
        || channels == nullptr || numChannels < 1 || numSamples <= 0
        || data[0].size() != static_cast<std::size_t>(capacity) || data[1].size() != static_cast<std::size_t>(capacity))
        return;

    const bool timingOk = playing && std::isfinite(beatAtStart) && finitePositive(samplesPerBeat);
    int first = 0;
    if (current == static_cast<int>(State::Armed))
    {
        if (!timingOk)
            return;
        const double bar = static_cast<double>(requestedBeatsPerBar.load(std::memory_order_relaxed));
        const double tolerance = kStartRoundingSamples / (samplesPerBeat * bar) + kBarTolerance;
        const double nextBar = std::ceil(beatAtStart / bar - tolerance) * bar;
        const double offset = (nextBar - beatAtStart) * samplesPerBeat;
        if (offset > static_cast<double>(numSamples) - 0.5)
            return; // the bar line starts in a later block
        first = static_cast<int>(std::llround(std::max(0.0, offset)));
        if (first >= numSamples)
            return;
        const double beats = static_cast<double>(requestedBars.load(std::memory_order_relaxed)) * bar;
        const double wantedExact = beats * samplesPerBeat;
        if (!(wantedExact <= static_cast<double>(bodyCapacity)))
        {
            fail(Failure::TooLong); // would not fit: refuse instead of truncating
            return;
        }
        target = std::llround(wantedExact);
        if (target < 1)
        {
            fail(Failure::Jumped);
            return;
        }
        total = target + std::min<std::int64_t>(requestedTail.load(std::memory_order_relaxed), capacity - target);
        written = 0;
        startSamplesPerBeat = samplesPerBeat;
        justStarted = true;
        recordBeats.store(beats, std::memory_order_relaxed);
        startBeatValue.store(nextBar, std::memory_order_relaxed);
        startBpmValue.store(rate * 60.0 / samplesPerBeat, std::memory_order_relaxed);
        bodyLength.store(target, std::memory_order_relaxed);
        totalStatus.store(total, std::memory_order_relaxed);
        writtenStatus.store(0, std::memory_order_relaxed);
        int expected = static_cast<int>(State::Armed);
        if (!stateValue.compare_exchange_strong(expected, static_cast<int>(State::Recording), std::memory_order_seq_cst))
            return; // cancelled while this block was being prepared
    }

    if (written < target)
    {
        // Body: the position must run continuously at a constant tempo.
        if (!playing)
        {
            fail(Failure::TransportStopped);
            return;
        }
        if (!timingOk)
        {
            fail(Failure::Jumped);
            return;
        }
        if (std::abs(samplesPerBeat - startSamplesPerBeat) > startSamplesPerBeat * kTempoTolerance)
        {
            fail(Failure::TempoChanged);
            return;
        }
        if (!justStarted)
        {
            const double toleranceSamples = std::max(4.0, 0.001 * rate);
            if (std::abs(beatAtStart - expectedBeat) * samplesPerBeat > toleranceSamples)
            {
                fail(Failure::Jumped);
                return;
            }
        }
        expectedBeat = beatAtStart + static_cast<double>(numSamples) / samplesPerBeat;
        justStarted = false;
    }

    const auto count = static_cast<int>(std::min<std::int64_t>(numSamples - first, total - written));
    float* left = data[0].data() + written;
    float* right = data[1].data() + written;
    const float* inL = channels[0] + first;
    const float* inR = channels[numChannels > 1 ? 1 : 0] + first;
    for (int i = 0; i < count; ++i)
    {
        left[i] = std::isfinite(inL[i]) ? inL[i] : 0.0f;
        right[i] = std::isfinite(inR[i]) ? inR[i] : 0.0f;
    }
    written += count;
    writtenStatus.store(written, std::memory_order_relaxed);
    if (written >= total)
    {
        recorded.store(written, std::memory_order_release);
        int expected = static_cast<int>(State::Recording);
        stateValue.compare_exchange_strong(expected, static_cast<int>(State::Done), std::memory_order_seq_cst);
    }
}

bool extractWet(float* out, const float* dry, float amount, int numSamples) noexcept
{
    if (out == nullptr || dry == nullptr || !(amount >= minWetAmount) || numSamples <= 0)
        return false;
    const float inverse = 1.0f / amount;
    for (int i = 0; i < numSamples; ++i)
        out[i] = dry[i] + (out[i] - dry[i]) * inverse;
    return true;
}
} // namespace groovemorph::core
