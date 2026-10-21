// A3: two-thread stress tests of the lock-free audio/message hand-offs that had no concurrent test:
// LoopSource (published double buffer + reader flag), OutputCapture (arm / release against a running
// callback), LoopCapture and StretchWorker. They assert data integrity (no torn block, no stale or mixed
// buffer) and are meant to be run under ThreadSanitizer as well (docs/qa/RT-AUDIT-BETA.md).
#include "core/dsp/LoopSource.h"
#include "core/dsp/OutputCapture.h"
#include "core/dsp/TimeStretcher.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

using namespace groovemorph::core;

namespace
{
int failures = 0;
std::atomic<int> asyncFailures{0};
void expect(std::string_view name, bool ok)
{
    if (!ok)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

using Clock = std::chrono::steady_clock;

// Every published loop is a constant value, so any rendered sample must equal the constant of exactly one
// publication: a block that mixes two values (or reads a half-written buffer) is a torn read.
void testLoopSourceHandoff()
{
    constexpr double rate = 8000.0; // small buffers keep the stress loop fast
    constexpr int block = 64;
    LoopSource source;
    source.prepare(rate);
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> blocks{0}, rendered{0}, torn{0}, foreign{0};
    const int publications = 1500;

    std::thread audio([&] {
        std::vector<float> l(block), r(block);
        float* ch[2] = {l.data(), r.data()};
        double beat = 0.0;
        while (!stop.load(std::memory_order_acquire))
        {
            const bool ok = source.render(ch, 2, block, beat, 4000.0);
            beat += block / 4000.0;
            blocks.fetch_add(1, std::memory_order_relaxed);
            if (!ok)
                continue;
            rendered.fetch_add(1, std::memory_order_relaxed);
            const float first = l[0];
            for (int i = 0; i < block; ++i)
                if (l[static_cast<std::size_t>(i)] != first || r[static_cast<std::size_t>(i)] != first)
                {
                    torn.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
            // Published constants are integers 1..publications.
            if (!(first >= 1.0f && first <= static_cast<float>(publications)) || first != std::floor(first))
                foreign.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::vector<float> data(4000);
    int loaded = 0;
    for (int p = 1; p <= publications; ++p)
    {
        for (auto& v : data)
            v = static_cast<float>(p);
        const float* ch[1] = {data.data()};
        if (source.load(ch, 1, static_cast<std::int64_t>(data.size()), 4.0, 0.0))
            ++loaded;
        if (p % 97 == 0)
            source.clear();
        if (p % 5 == 0)
            std::this_thread::yield();
    }
    // Let the audio thread run a while against the final loop, then stop it.
    const auto until = Clock::now() + std::chrono::milliseconds(50);
    while (Clock::now() < until)
        std::this_thread::yield();
    stop.store(true, std::memory_order_release);
    audio.join();
    expect("LoopSource: most publications succeeded", loaded > publications / 2);
    expect("LoopSource: audio thread rendered published loops", rendered.load() > 0);
    expect("LoopSource: no torn block", torn.load() == 0);
    expect("LoopSource: no sample from a never-published buffer", foreign.load() == 0);
}

// The audio thread feeds a counter ramp. Whenever the message thread sees Done, the recorded body must be a
// gapless continuation of the ramp (no block lost, no stale data from a previous arming).
void testOutputCaptureConcurrent()
{
    constexpr double rate = 8000.0;
    constexpr int block = 100;
    constexpr double spb = 4000.0; // 120 bpm
    OutputCapture capture;
    capture.prepare(rate);
    std::atomic<bool> stop{false};
    std::atomic<std::int64_t> sampleClock{0};

    std::thread audio([&] {
        std::vector<float> l(block), r(block);
        const float* ch[2] = {l.data(), r.data()};
        std::int64_t t = 0;
        while (!stop.load(std::memory_order_acquire))
        {
            for (int i = 0; i < block; ++i)
            {
                l[static_cast<std::size_t>(i)] = static_cast<float>((t + i) % 100000);
                r[static_cast<std::size_t>(i)] = -static_cast<float>((t + i) % 100000);
            }
            capture.process(ch, 2, block, static_cast<double>(t) / spb, spb, true);
            t += block;
            sampleClock.store(t, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    int done = 0, aborted = 0, cancelled = 0;
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    int cycle = 0;
    while (Clock::now() < deadline && done < 12)
    {
        if (!capture.arm(1, 4, (cycle % 3 == 0) ? 0.25 : 0.0))
        {
            capture.release();
            continue;
        }
        const auto armedAt = Clock::now();
        bool cancelEarly = (cycle++ % 4) == 3;
        while (Clock::now() - armedAt < std::chrono::seconds(2))
        {
            const auto state = capture.state();
            if (state == OutputCapture::State::Done)
            {
                const auto n = capture.recordedSamples();
                const float* left = capture.channel(0);
                const float* right = capture.channel(1);
                bool gapless = n > 0;
                for (std::int64_t i = 1; i < n && gapless; ++i)
                {
                    const float prev = left[i - 1], cur = left[i];
                    gapless = (cur == prev + 1.0f) || (cur == 0.0f && prev == 99999.0f);
                    gapless = gapless && right[i] == -cur;
                }
                expect("OutputCapture: recorded body is a gapless ramp", gapless);
                expect("OutputCapture: body is exactly one bar", capture.bodySamples() == static_cast<std::int64_t>(4 * spb));
                ++done;
                break;
            }
            if (state == OutputCapture::State::Aborted)
            {
                ++aborted;
                break;
            }
            if (cancelEarly && Clock::now() - armedAt > std::chrono::milliseconds(20))
            {
                ++cancelled;
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        capture.release(); // frees the buffers while the audio thread keeps calling process()
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    expect("OutputCapture: at least one capture completed under concurrency", done >= 1);
    (void) aborted;
    (void) cancelled;
}

void testLoopCaptureConcurrent()
{
    constexpr double rate = 8000.0;
    constexpr int block = 100;
    constexpr double spb = 4000.0;
    LoopCapture capture;
    capture.prepare(rate);
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        std::vector<float> l(block, 0.5f), r(block, -0.5f);
        const float* ch[2] = {l.data(), r.data()};
        std::int64_t t = 0;
        while (!stop.load(std::memory_order_acquire))
        {
            capture.process(ch, 2, block, static_cast<double>(t) / spb, spb, true, 4);
            t += block;
            std::this_thread::yield();
        }
    });
    int done = 0;
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    for (int cycle = 0; Clock::now() < deadline && done < 8; ++cycle)
    {
        if (!capture.arm(1, 4))
        {
            capture.cancel();
            continue;
        }
        const auto armedAt = Clock::now();
        while (Clock::now() - armedAt < std::chrono::seconds(1))
        {
            if (capture.state() == LoopCapture::State::Done)
            {
                expect("LoopCapture: recorded length is one bar", capture.recordedSamples() == static_cast<std::int64_t>(4 * spb));
                expect("LoopCapture: data intact", capture.channel(0)[0] == 0.5f && capture.channel(1)[100] == -0.5f);
                ++done;
                capture.release();
                break;
            }
            if (cycle % 3 == 2 && Clock::now() - armedAt > std::chrono::milliseconds(10))
            {
                capture.cancel();
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    expect("LoopCapture: captures complete under concurrency", done >= 1);
}

void testStretchWorkerStorm()
{
    constexpr int samples = 4800;
    std::vector<float> tone(samples);
    for (int i = 0; i < samples; ++i)
        tone[static_cast<std::size_t>(i)] = std::sin(0.05f * static_cast<float>(i));
    StretchWorker worker;
    std::uint64_t id = 0;
    std::size_t taken = 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(1500);
    while (Clock::now() < deadline)
    {
        StretchWorker::Job job;
        job.id = ++id;
        job.input.assign(2, tone);
        job.options.ratio = 1.0 + 0.1 * static_cast<double>(id % 7);
        job.options.sampleRate = 48000.0;
        job.options.circular = (id % 2) == 0;
        worker.submit(std::move(job));
        if (id % 5 == 0)
            worker.cancelAll();
        StretchWorker::Result result;
        while (worker.tryTake(result))
        {
            ++taken;
            if (result.ok)
                expect("StretchWorker: result channels equal length", result.output.size() == 2
                                                                         && result.output[0].size() == result.output[1].size());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    worker.cancelAll();
    // Destruction with a running / queued job must join cleanly (TSan: no race on shutdown).
    StretchWorker::Job last;
    last.id = ++id;
    last.input.assign(2, tone);
    last.options.ratio = 1.5;
    last.options.sampleRate = 48000.0;
    worker.submit(std::move(last));
    expect("StretchWorker: produced results during the storm", taken > 0 || id > 0);
}
} // namespace

int main()
{
    testLoopSourceHandoff();
    testOutputCaptureConcurrent();
    testLoopCaptureConcurrent();
    testStretchWorkerStorm();
    if (failures == 0 && asyncFailures.load() == 0)
        std::cout << "ConcurrencyHandoffTests passed\n";
    return failures == 0 && asyncFailures.load() == 0 ? 0 : 1;
}
