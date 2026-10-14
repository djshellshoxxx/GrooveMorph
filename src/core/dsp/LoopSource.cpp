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
    for (auto& d : data)
        d.assign(static_cast<std::size_t>(capacity) * maxChannels, 0.0f);
    counterAtPublish = renderCounter.load(std::memory_order_acquire);
}

bool LoopSource::load(const float* const* channels, int numChannels, std::int64_t numSamples,
                      double loopBeats, double originBeats)
{
    if (capacity <= 0 || channels == nullptr || numChannels < 1 || numSamples < 16
        || !(loopBeats > 0.0) || !std::isfinite(loopBeats) || !std::isfinite(originBeats))
        return false;
    // Wait (bounded) until the audio thread has finished any block that began with the buffer we
    // are about to overwrite. With no audio running the counter never moves and we proceed.
    for (int i = 0; i < 200 && renderCounter.load(std::memory_order_acquire) == counterAtPublish
                    && active.load(std::memory_order_acquire) >= 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const int current = active.load(std::memory_order_acquire);
    const int idle = current == 0 ? 1 : 0;
    const std::int64_t n = std::min(numSamples, capacity);
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
    counterAtPublish = renderCounter.load(std::memory_order_acquire);
    active.store(idle, std::memory_order_release);
    return true;
}

void LoopSource::clear() noexcept
{
    active.store(-1, std::memory_order_release);
}

bool LoopSource::render(float* const* channels, int numChannels, int numSamples, double beatAtStart,
                        double samplesPerBeat) noexcept
{
    const int index = active.load(std::memory_order_acquire);
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
    renderCounter.fetch_add(1, std::memory_order_release);
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
    requestedBars.store(bars, std::memory_order_relaxed);
    requestedBeatsPerBar.store(beatsPerBar, std::memory_order_relaxed);
    stateValue.store(static_cast<int>(State::Armed), std::memory_order_release);
    return true;
}

void LoopCapture::cancel() noexcept
{
    stateValue.store(static_cast<int>(State::Idle), std::memory_order_release);
}

void LoopCapture::process(const float* const* channels, int numChannels, int numSamples,
                          double beatAtStart, double samplesPerBeat, bool playing) noexcept
{
    const auto current = state();
    if ((current != State::Armed && current != State::Recording) || channels == nullptr || numSamples <= 0
        || !playing || !(samplesPerBeat > 0.0) || !std::isfinite(beatAtStart) || !std::isfinite(samplesPerBeat))
        return;
    int first = 0;
    if (current == State::Armed)
    {
        const double bar = static_cast<double>(requestedBeatsPerBar.load(std::memory_order_relaxed));
        const double nextBar = std::ceil(beatAtStart / bar - 1.0e-9) * bar;
        const double offset = (nextBar - beatAtStart) * samplesPerBeat;
        if (offset >= static_cast<double>(numSamples))
            return; // the bar line is in a later block
        first = static_cast<int>(std::llround(std::max(0.0, offset)));
        if (first >= numSamples)
            return;
        const double beats = static_cast<double>(requestedBars.load(std::memory_order_relaxed)) * bar;
        target = std::min(capacity, static_cast<std::int64_t>(std::llround(beats * samplesPerBeat)));
        written = 0;
        recordBeats.store(beats, std::memory_order_relaxed);
        startBeatValue.store(nextBar, std::memory_order_relaxed);
        stateValue.store(static_cast<int>(State::Recording), std::memory_order_release);
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
        stateValue.store(static_cast<int>(State::Done), std::memory_order_release);
    }
}
} // namespace groovemorph::core
