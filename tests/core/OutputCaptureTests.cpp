#include "core/dsp/OutputCapture.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>
#include <vector>

using namespace groovemorph::core;

namespace
{
int failures = 0;
void expect(std::string_view name, bool ok)
{
    if (!ok)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

// Deterministic test signal: sample k of the "timeline" has value k + 1 (exact in float below 2^24).
float signalAt(std::int64_t k) { return static_cast<float>(k + 1); }

struct Driver
{
    double rate = 48000.0;
    double spb = 24000.0; // samples per beat
    // Feeds [from, to) timeline samples in blocks produced by `next()`. Returns the next timeline sample.
    template <class Next>
    std::int64_t run(OutputCapture& oc, std::int64_t from, std::int64_t to, Next next, bool playing = true)
    {
        std::vector<float> left, right;
        std::int64_t k = from;
        while (k < to)
        {
            const int n = static_cast<int>(std::min<std::int64_t>(next(), to - k));
            left.resize(static_cast<std::size_t>(n));
            right.resize(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
            {
                left[static_cast<std::size_t>(i)] = signalAt(k + i);
                right[static_cast<std::size_t>(i)] = -signalAt(k + i);
            }
            const float* ch[2] = {left.data(), right.data()};
            oc.process(ch, 2, n, static_cast<double>(k) / spb, spb, playing);
            k += n;
        }
        return k;
    }
};

auto fixedBlocks(int n) { return [n] { return n; }; }

struct Lcg
{
    std::uint32_t state;
    int operator()() { state = state * 1664525u + 1013904223u; return 1 + static_cast<int>((state >> 8) % 997u); }
};
} // namespace

int main()
{
    // N04-C01: validation and default state
    {
        OutputCapture oc;
        oc.prepare(48000.0);
        expect("C01 idle by default", oc.state() == OutputCapture::State::Idle);
        expect("C01 bars 0 rejected", !oc.arm(0, 4, 0.0));
        expect("C01 bars 17 rejected", !oc.arm(17, 4, 0.0));
        expect("C01 beats 0 rejected", !oc.arm(1, 0, 0.0));
        expect("C01 beats 17 rejected", !oc.arm(1, 17, 0.0));
        expect("C01 negative tail rejected", !oc.arm(1, 4, -1.0));
        expect("C01 tail over cap rejected", !oc.arm(1, 4, 9.0));
        expect("C01 NaN tail rejected", !oc.arm(1, 4, std::numeric_limits<double>::quiet_NaN()));
        expect("C01 still idle", oc.state() == OutputCapture::State::Idle);
        expect("C01 valid arm", oc.arm(16, 16, 8.0));
        expect("C01 armed", oc.state() == OutputCapture::State::Armed);
        expect("C01 busy arm rejected", !oc.arm(1, 4, 0.0));
        oc.cancel();
        expect("C01 cancel idles", oc.state() == OutputCapture::State::Idle);
        OutputCapture unprepared;
        expect("C01 unprepared arm rejected", !unprepared.arm(1, 4, 0.0));
    }

    // N04-C02 / C04: bar-aligned start, exact length
    {
        Driver d;
        d.spb = 24000.0; // 120 BPM at 48 kHz, bar = 96000
        OutputCapture oc;
        oc.prepare(48000.0);
        expect("C02 arm", oc.arm(2, 4, 0.0));
        d.run(oc, 1000, 96000, fixedBlocks(512));
        expect("C02 waits for the bar line", oc.state() == OutputCapture::State::Armed);
        d.run(oc, 96000, 96000 + 2 * 96000 + 5000, fixedBlocks(512));
        expect("C02 done", oc.state() == OutputCapture::State::Done);
        expect("C02 length", oc.recordedSamples() == 2 * 96000 && oc.bodySamples() == 2 * 96000);
        expect("C02 start beat", oc.startBeat() == 4.0);
        expect("C02 loop beats", oc.loopBeats() == 8.0);
        expect("C02 bpm", std::abs(oc.startBpm() - 120.0) < 1.0e-9);
        expect("C02 first sample", oc.channel(0)[0] == signalAt(96000));
        expect("C02 last sample", oc.channel(0)[2 * 96000 - 1] == signalAt(96000 + 2 * 96000 - 1));
        expect("C02 right channel", oc.channel(1)[10] == -signalAt(96010));
        oc.release();
        expect("C02 release idles", oc.state() == OutputCapture::State::Idle);

        // C04: arming exactly on the bar line starts at offset 0
        expect("C04 arm", oc.arm(1, 4, 0.0));
        d.run(oc, 96000, 96000 + 96000 + 100, fixedBlocks(480));
        expect("C04 done", oc.state() == OutputCapture::State::Done);
        expect("C04 first sample at the bar line", oc.channel(0)[0] == signalAt(96000));
        expect("C04 length", oc.recordedSamples() == 96000);
    }

    // N04-C03: block-split invariance with a non-integer samples-per-beat
    {
        Driver d;
        d.spb = 60.0 * 48000.0 / 137.3; // non-integer
        const std::int64_t bar = static_cast<std::int64_t>(std::llround(2.0 * d.spb)); // bpb = 2
        std::vector<float> reference;
        std::int64_t referenceBody = 0;
        for (int variant = 0; variant < 9; ++variant)
        {
            OutputCapture oc;
            oc.prepare(48000.0);
            expect("C03 arm", oc.arm(3, 2, 0.0));
            const std::int64_t startAt = 777;
            const std::int64_t end = startAt + 4 * bar + 2000;
            if (variant == 0)
                d.run(oc, startAt, end, fixedBlocks(64));
            else if (variant == 1)
                d.run(oc, startAt, end, fixedBlocks(1));
            else if (variant == 2)
                d.run(oc, startAt, end, fixedBlocks(4096));
            else if (variant == 3)
                d.run(oc, startAt, end, fixedBlocks(333));
            else
                d.run(oc, startAt, end, Lcg{static_cast<std::uint32_t>(variant * 7919)});
            expect("C03 done", oc.state() == OutputCapture::State::Done);
            if (oc.state() != OutputCapture::State::Done)
                continue;
            std::vector<float> got(oc.channel(0), oc.channel(0) + oc.recordedSamples());
            if (variant == 0)
            {
                reference = got;
                referenceBody = oc.bodySamples();
                expect("C03 length", referenceBody == std::llround(6.0 * d.spb));
            }
            else
                expect("C03 identical to the reference", got == reference && oc.bodySamples() == referenceBody);
            oc.release();
        }
    }

    // N04-C05: aborts
    {
        Driver d;
        d.spb = 24000.0;
        const auto armed = [](OutputCapture& oc) { oc.prepare(48000.0); return oc.arm(1, 4, 0.0); };
        {
            OutputCapture oc;
            expect("C05 arm", armed(oc));
            std::int64_t k = d.run(oc, 90000, 96000 + 20000, fixedBlocks(512));
            expect("C05 recording", oc.state() == OutputCapture::State::Recording);
            d.run(oc, k, k + 512, fixedBlocks(512), false);
            expect("C05 transport stop aborts", oc.state() == OutputCapture::State::Aborted
                                                  && oc.failure() == OutputCapture::Failure::TransportStopped);
            d.run(oc, k + 512, k + 5000, fixedBlocks(512));
            expect("C05 aborted stays aborted", oc.state() == OutputCapture::State::Aborted);
            oc.release();
            expect("C05 release idles", oc.state() == OutputCapture::State::Idle);
        }
        {
            OutputCapture oc;
            expect("C05 arm seek", armed(oc));
            std::int64_t k = d.run(oc, 90000, 96000 + 20000, fixedBlocks(512));
            d.run(oc, k + 48000, k + 48000 + 512, fixedBlocks(512)); // jumped forward
            expect("C05 seek aborts", oc.state() == OutputCapture::State::Aborted
                                          && oc.failure() == OutputCapture::Failure::Jumped);
        }
        {
            OutputCapture oc;
            expect("C05 arm tempo", armed(oc));
            std::int64_t k = d.run(oc, 90000, 96000 + 20000, fixedBlocks(512));
            Driver faster = d;
            faster.spb = 23000.0;
            // keep the beat continuous across the change so only the tempo differs
            std::vector<float> block(512, 1.0f);
            const float* ch[1] = {block.data()};
            oc.process(ch, 1, 512, static_cast<double>(k) / d.spb, faster.spb, true);
            expect("C05 tempo change aborts", oc.state() == OutputCapture::State::Aborted
                                                  && oc.failure() == OutputCapture::Failure::TempoChanged);
        }
        {
            OutputCapture oc;
            expect("C05 arm nan", armed(oc));
            std::int64_t k = d.run(oc, 90000, 96000 + 20000, fixedBlocks(512));
            std::vector<float> block(64, 1.0f);
            const float* ch[1] = {block.data()};
            oc.process(ch, 1, 64, std::numeric_limits<double>::quiet_NaN(), d.spb, true);
            expect("C05 invalid timing aborts", oc.state() == OutputCapture::State::Aborted);
            (void) k;
        }
        {
            OutputCapture oc;
            expect("C05 arm abortBody", armed(oc));
            d.run(oc, 90000, 96000 + 20000, fixedBlocks(512));
            oc.abortBody(OutputCapture::Failure::Jumped);
            expect("C05 abortBody aborts", oc.state() == OutputCapture::State::Aborted);
        }
        {
            // Not playing while armed just waits
            OutputCapture oc;
            expect("C05 arm idle wait", armed(oc));
            d.run(oc, 90000, 200000, fixedBlocks(512), false);
            expect("C05 armed survives stopped transport", oc.state() == OutputCapture::State::Armed);
        }
    }

    // N04-C06: hard cap
    {
        Driver d;
        d.spb = 96000.0; // 30 BPM at 48 kHz: 16 bars = 6.1 M samples > 90 s
        OutputCapture oc;
        oc.prepare(48000.0);
        expect("C06 arm", oc.arm(16, 4, 0.0));
        d.run(oc, 0, 5000, fixedBlocks(256));
        expect("C06 refused at the bar line", oc.state() == OutputCapture::State::Aborted
                                                  && oc.failure() == OutputCapture::Failure::TooLong);
        expect("C06 nothing recorded", oc.recordedSamples() == 0);
        oc.release();
        // Exactly 90 s is allowed: 90 s = 4320000 samples = 45 beats at 96000 -> 9 bars of 5 beats... use bpb 5, 9 bars
        d.spb = 96000.0;
        expect("C06 arm at the cap", oc.arm(9, 5, 0.0)); // 45 beats * 96000 = 4.32 M = exactly the cap
        d.run(oc, 0, 5000, fixedBlocks(256));
        expect("C06 cap exactly is accepted", oc.state() == OutputCapture::State::Recording);
        oc.release();
    }

    // N04-C07: tail
    {
        Driver d;
        d.spb = 24000.0;
        OutputCapture oc;
        oc.prepare(48000.0);
        expect("C07 arm", oc.arm(1, 4, 0.1)); // 4800 tail samples
        std::int64_t k = d.run(oc, 96000, 96000 + 96000 + 1000, fixedBlocks(480)); // tail begun, 1000 samples in
        expect("C07 recording tail", oc.state() == OutputCapture::State::Recording);
        expect("C07 progress inside tail", oc.progress() > 0.95 && oc.progress() < 1.0);
        // The transport stops during the tail: tail keeps filling with whatever is output next.
        oc.abortBody(OutputCapture::Failure::Jumped);
        expect("C07 abortBody ignored in the tail", oc.state() == OutputCapture::State::Recording);
        d.run(oc, k, k + 6000, fixedBlocks(480), false);
        expect("C07 done after tail", oc.state() == OutputCapture::State::Done);
        expect("C07 total", oc.recordedSamples() == 96000 + 4800);
        expect("C07 body", oc.bodySamples() == 96000);
        expect("C07 tail content continues", oc.channel(0)[96000 + 10] == signalAt(96000 + 96000 + 10));
        expect("C07 progress done", oc.progress() == 1.0);
        oc.release();
        // A tail that would exceed the cap is clamped, never overruns
        expect("C07 clamp arm", oc.arm(9, 5, 8.0));
        d.spb = 96000.0;
        d.run(oc, 0, 96000 * 46 + 8 * 48000 + 1000, fixedBlocks(4096));
        expect("C07 clamped done", oc.state() == OutputCapture::State::Done
                                       && oc.recordedSamples() == oc.capacitySamples());
    }

    // N04-C08: cancel, NaN, mono
    {
        Driver d;
        d.spb = 24000.0;
        OutputCapture oc;
        oc.prepare(48000.0);
        expect("C08 arm", oc.arm(1, 4, 0.0));
        std::int64_t k = d.run(oc, 90000, 96000 + 5000, fixedBlocks(512));
        expect("C08 recording", oc.state() == OutputCapture::State::Recording);
        oc.cancel();
        expect("C08 cancel idles", oc.state() == OutputCapture::State::Idle);
        d.run(oc, k, k + 2000, fixedBlocks(512));
        expect("C08 idle ignores audio", oc.state() == OutputCapture::State::Idle);
        expect("C08 re-arm", oc.arm(1, 4, 0.0));
        // mono input with NaN/inf samples, starting on a bar line
        std::vector<float> mono(96000);
        for (std::size_t i = 0; i < mono.size(); ++i)
            mono[i] = static_cast<float>(i + 1);
        mono[10] = std::numeric_limits<float>::quiet_NaN();
        mono[11] = std::numeric_limits<float>::infinity();
        const float* ch[1] = {mono.data()};
        for (std::size_t pos = 0; pos < mono.size(); pos += 4800)
            oc.process(ch, 1, 0, 0.0, 24000.0, true); // zero-length blocks are ignored
        for (std::size_t pos = 0; pos < mono.size(); pos += 4800)
        {
            const float* block[1] = {mono.data() + pos};
            oc.process(block, 1, 4800, static_cast<double>(192000 + pos) / 24000.0, 24000.0, true);
        }
        expect("C08 mono done", oc.state() == OutputCapture::State::Done);
        expect("C08 NaN stored as zero", oc.channel(0)[10] == 0.0f && oc.channel(0)[11] == 0.0f);
        expect("C08 mono duplicated", oc.channel(0)[500] == oc.channel(1)[500] && oc.channel(1)[500] == 501.0f);
    }

    // N04-C09: progress and metadata
    {
        Driver d;
        d.spb = 24000.0;
        OutputCapture oc;
        oc.prepare(48000.0);
        expect("C09 progress idle", oc.progress() == 0.0);
        expect("C09 arm", oc.arm(1, 4, 0.0));
        double last = 0.0;
        bool monotonic = true;
        std::int64_t k = 96000;
        while (k < 96000 + 96000 + 960 && oc.state() != OutputCapture::State::Done)
        {
            k = d.run(oc, k, k + 960, fixedBlocks(960));
            const double p = oc.progress();
            monotonic = monotonic && p >= last && p <= 1.0;
            last = p;
        }
        expect("C09 monotonic", monotonic);
        expect("C09 reaches 1", oc.progress() == 1.0);
        expect("C09 start beat", oc.startBeat() == 4.0);
    }

    // N04-C10: wet extraction
    {
        const float dry[4] = {0.5f, -0.25f, 1.0f, 0.0f};
        const float wet[4] = {0.1f, 0.9f, -0.5f, 0.3f};
        const float amount = 0.4f;
        float out[4];
        for (int i = 0; i < 4; ++i)
            out[i] = dry[i] + amount * (wet[i] - dry[i]);
        expect("C10 extract applies", extractWet(out, dry, amount, 4));
        bool close = true;
        for (int i = 0; i < 4; ++i)
            close = close && std::abs(out[i] - wet[i]) < 1.0e-6f;
        expect("C10 recovers wet", close);
        float pass[2] = {0.7f, 0.8f};
        expect("C10 below minimum passes through", !extractWet(pass, dry, 0.005f, 2) && pass[0] == 0.7f && pass[1] == 0.8f);
        expect("C10 null guarded", !extractWet(nullptr, dry, 0.5f, 2) && !extractWet(pass, nullptr, 0.5f, 2));
    }

    // N04-C11: memory freed on release, reallocated on arm; audio thread racing cancel/release
    {
        OutputCapture oc;
        oc.prepare(8000.0);
        expect("C11 arm", oc.arm(1, 4, 0.0));
        oc.release();
        expect("C11 re-arm after release", oc.arm(1, 4, 0.5));
        oc.release();

        std::atomic<bool> stop{false};
        std::thread audio([&] {
            std::vector<float> block(64, 0.25f);
            const float* ch[2] = {block.data(), block.data()};
            std::int64_t k = 0;
            const double spb = 2000.0; // 8 kHz, 240 BPM
            while (!stop.load())
            {
                oc.process(ch, 2, 64, static_cast<double>(k) / spb, spb, true);
                k += 64;
            }
        });
        int armed = 0;
        for (int i = 0; i < 400; ++i)
        {
            if (oc.arm(1, 1, 0.01))
                ++armed;
            std::this_thread::sleep_for(std::chrono::microseconds(150 + (i % 7) * 50));
            if (oc.state() == OutputCapture::State::Done)
            {
                volatile float sink = oc.channel(0)[0] + oc.channel(1)[oc.recordedSamples() - 1];
                (void) sink;
            }
            oc.release();
        }
        stop.store(true);
        audio.join();
        expect("C11 stress armed repeatedly", armed > 0);
        expect("C11 stress ends idle", oc.state() == OutputCapture::State::Idle);
    }

    if (failures == 0)
        std::cout << "OutputCaptureTests passed\n";
    return failures == 0 ? 0 : 1;
}
