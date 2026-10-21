#include "core/dsp/LoopSource.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace groovemorph::core
{
void LoopSource::prepare(double sampleRate)
{
    rate = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
    capacity = static_cast<std::int64_t>(std::ceil(maxSeconds * rate));
    active.store(-1, std::memory_order_release);
    lastPublished = -1;
    for (auto& d : data)
        d.assign(static_cast<std::size_t>(capacity) * maxChannels, 0.0f);
}

bool LoopSource::load(const float* const* channels, int numChannels, std::int64_t numSamples,
                      double loopBeats, double originBeats)
{
    if (capacity <= 0 || channels == nullptr || numChannels < 1 || numSamples < 16
        || !(loopBeats > 0.0) || !std::isfinite(loopBeats) || !std::isfinite(originBeats))
        return false;
    // Write the buffer that was not last published, once the audio thread is not reading it.
    const int idle = lastPublished == 0 ? 1 : 0;
    for (int i = 0; i < 500 && reading.load(std::memory_order_seq_cst) == idle; ++i)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    if (reading.load(std::memory_order_seq_cst) == idle)
        return false; // the audio thread is stuck on this buffer; refuse rather than race
    if (numSamples > capacity)
        return false; // truncating would silently stretch the loop against its beat length
    const std::int64_t n = numSamples;
    auto& buffer = data[idle];
    for (int c = 0; c < maxChannels; ++c)
    {
        const float* src = channels[std::min(c, numChannels - 1)];
        float* dst = buffer.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(capacity);
        for (std::int64_t i = 0; i < n; ++i)
            dst[i] = std::isfinite(src[i]) ? src[i] : 0.0f;
    }
    length[idle].store(n, std::memory_order_relaxed);
    beats[idle].store(loopBeats, std::memory_order_relaxed);
    origin[idle].store(std::fmod(originBeats, loopBeats), std::memory_order_relaxed);
    lastPublished = idle;
    active.store(idle, std::memory_order_seq_cst);
    return true;
}

void LoopSource::clear() noexcept
{
    active.store(-1, std::memory_order_release);
}

bool LoopSource::render(float* const* channels, int numChannels, int numSamples, double beatAtStart,
                        double samplesPerBeat) noexcept
{
    int index = active.load(std::memory_order_seq_cst);
    if (index >= 0)
    {
        reading.store(index, std::memory_order_seq_cst);
        if (active.load(std::memory_order_seq_cst) != index) // republished meanwhile: back off
        {
            reading.store(-1, std::memory_order_seq_cst);
            index = -1;
        }
    }
    bool rendered = false;
    if (index >= 0 && channels != nullptr && numSamples > 0 && samplesPerBeat > 0.0
        && std::isfinite(samplesPerBeat) && std::isfinite(beatAtStart))
    {
        const std::int64_t len = length[index].load(std::memory_order_relaxed);
        const double loop = beats[index].load(std::memory_order_relaxed);
        const double org = origin[index].load(std::memory_order_relaxed);
        const float* base = data[index].data();
        if (len >= 2 && loop > 0.0)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                double phase = std::fmod(beatAtStart + static_cast<double>(i) / samplesPerBeat - org, loop);
                if (phase < 0.0)
                    phase += loop;
                const double pos = phase / loop * static_cast<double>(len);
                const auto i0 = static_cast<std::int64_t>(pos);
                const double frac = pos - static_cast<double>(i0);
                const std::int64_t a = i0 % len;
                const std::int64_t b = (a + 1) % len;
                for (int c = 0; c < numChannels; ++c)
                {
                    const float* ch = base + static_cast<std::size_t>(std::min(c, maxChannels - 1))
                                                 * static_cast<std::size_t>(capacity);
                    channels[c][i] = static_cast<float>(ch[a] + (ch[b] - ch[a]) * frac);
                }
            }
            rendered = true;
        }
    }
    if (index >= 0)
        reading.store(-1, std::memory_order_seq_cst);
    return rendered;
}

void LoopCapture::prepare(double sampleRate)
{
    const double rate = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
    capacity = static_cast<std::int64_t>(std::ceil(LoopSource::maxSeconds * rate));
    for (auto& d : data)
        d.assign(static_cast<std::size_t>(capacity), 0.0f);
    stateValue.store(static_cast<int>(State::Idle), std::memory_order_release);
}

bool LoopCapture::arm(int bars, int beatsPerBar)
{
    if (capacity <= 0 || bars < 1 || bars > 16 || beatsPerBar < 1 || beatsPerBar > 16
        || state() != State::Idle)
        return false;
    failureReason.store(0, std::memory_order_relaxed);
    requestedBars.store(bars, std::memory_order_relaxed);
    requestedBeatsPerBar.store(beatsPerBar, std::memory_order_relaxed);
    stateValue.store(static_cast<int>(State::Armed), std::memory_order_release);
    return true;
}

void LoopCapture::cancel() noexcept
{
    stateValue.store(static_cast<int>(State::Idle), std::memory_order_seq_cst);
}

void LoopCapture::abort() noexcept
{
    int expected = static_cast<int>(State::Recording);
    if (stateValue.compare_exchange_strong(expected, static_cast<int>(State::Idle), std::memory_order_seq_cst))
        failureReason.store(2, std::memory_order_relaxed);
}

void LoopCapture::process(const float* const* channels, int numChannels, int numSamples,
                          double beatAtStart, double samplesPerBeat, bool playing, int beatsPerBar) noexcept
{
    const auto current = state();
    if ((current != State::Armed && current != State::Recording) || channels == nullptr || numSamples <= 0
        || !playing || !(samplesPerBeat > 0.0) || !std::isfinite(beatAtStart) || !std::isfinite(samplesPerBeat))
        return;
    const auto failWith = [this](int reason) noexcept {
        failureReason.store(reason, std::memory_order_relaxed);
        stateValue.store(static_cast<int>(State::Idle), std::memory_order_seq_cst);
    };
    if (beatsPerBar > 0 && beatsPerBar != requestedBeatsPerBar.load(std::memory_order_relaxed))
    {
        failWith(4); // the bar length this capture was armed with no longer exists
        return;
    }
    if (current == State::Recording && std::abs(samplesPerBeat - startSamplesPerBeat) > 1.0e-3 * startSamplesPerBeat)
    {
        failWith(3); // the recorded length would no longer match its beat length
        return;
    }
    int first = 0;
    if (current == State::Armed)
    {
        const double bar = static_cast<double>(requestedBeatsPerBar.load(std::memory_order_relaxed));
        const double nextBar = std::ceil(beatAtStart / bar - (0.5 / (samplesPerBeat * bar) + 1.0e-9)) * bar;
        const double offset = (nextBar - beatAtStart) * samplesPerBeat;
        // Sample-accurate to the nearest sample: a bar line within half a sample after this block's start is
        // this block's first sample, one that rounds to the block length belongs to the next block.
        const auto firstRounded = std::llround(std::max(0.0, offset));
        if (firstRounded >= numSamples)
            return; // the bar line is in a later block
        first = static_cast<int>(firstRounded);
        const double beats = static_cast<double>(requestedBars.load(std::memory_order_relaxed)) * bar;
        const double wantedExact = beats * samplesPerBeat;
        if (!(wantedExact >= 1.0) || !std::isfinite(wantedExact))
        {
            failureReason.store(1, std::memory_order_relaxed); // absurd tempo (length < 1 sample or not finite): llround would be undefined (A3-05)
            int armedState = static_cast<int>(State::Armed);
            stateValue.compare_exchange_strong(armedState, static_cast<int>(State::Idle), std::memory_order_seq_cst);
            return;
        }
        const auto wanted = wantedExact > static_cast<double>(capacity) ? capacity + 1
                                                                       : static_cast<std::int64_t>(std::llround(wantedExact));
        if (wanted > capacity)
        {
            failureReason.store(1, std::memory_order_relaxed); // would not fit: refuse instead of truncating
            int armedState = static_cast<int>(State::Armed);
            stateValue.compare_exchange_strong(armedState, static_cast<int>(State::Idle), std::memory_order_seq_cst);
            return;
        }
        target = wanted;
        written = 0;
        startSamplesPerBeat = samplesPerBeat;
        recordBeats.store(beats, std::memory_order_relaxed);
        startBeatValue.store(nextBar, std::memory_order_relaxed);
        int expected = static_cast<int>(State::Armed);
        if (!stateValue.compare_exchange_strong(expected, static_cast<int>(State::Recording), std::memory_order_seq_cst))
            return; // cancelled or re-armed while this block was being prepared
    }
    const int count = static_cast<int>(std::min<std::int64_t>(numSamples - first, target - written));
    for (int i = 0; i < count; ++i)
    {
        data[0][static_cast<std::size_t>(written + i)] = channels[0][first + i];
        data[1][static_cast<std::size_t>(written + i)] = channels[numChannels > 1 ? 1 : 0][first + i];
    }
    written += count;
    if (written >= target)
    {
        recorded.store(written, std::memory_order_relaxed);
        int expected = static_cast<int>(State::Recording);
        stateValue.compare_exchange_strong(expected, static_cast<int>(State::Done), std::memory_order_seq_cst);
    }
}
} // namespace groovemorph::core
