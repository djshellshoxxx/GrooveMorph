#include "core/Scheduler.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

using namespace groovemorph::core;

namespace
{
int failures = 0;

void expect(std::string_view name, bool condition)
{
    if (!condition)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

ResolvedTiming timingAt(std::int64_t sample, double tempo, double rate, bool playing = true,
                        int numerator = 4, int phraseBars = 2)
{
    const HostTransport host{tempo, numerator, 4, playing, sample};
    return *TransportResolver::resolve(host, PhraseGrid{120.0, 4, 4, phraseBars, 0}, 0, rate);
}

// Runs blocks from startSample until a switch or limit; returns absolute switch sample or -1.
std::int64_t runUntilSwitch(Scheduler& s, std::int64_t startSample, int blockSize, double tempo,
                            double rate, std::int64_t limit)
{
    for (std::int64_t block = startSample; block < limit; block += blockSize)
    {
        const auto event = s.process(timingAt(block, tempo, rate), blockSize);
        if (event.switched)
        {
            if (event.sampleOffset < 0 || event.sampleOffset >= blockSize)
                return -2;
            return block + event.sampleOffset;
        }
    }
    return -1;
}

std::int64_t expectedBoundary(std::int64_t from, double tempo, double rate, double beats)
{
    const long double interval = static_cast<long double>(rate) * 60.0L / tempo * beats;
    const auto n = std::ceil(static_cast<long double>(from) / interval - 1.0e-12L);
    return static_cast<std::int64_t>(std::round(n * interval));
}
} // namespace

int main()
{
    // Timeline tests at several tempos, rates and block sizes.
    const double tempos[] = {120.0, 97.0, 173.0, 60.5};
    const double rates[] = {48000.0, 44100.0, 96000.0};
    const int blocks[] = {1, 64, 441, 512, 1000, 4096};
    struct ModeCase { TransitionMode mode; double beats; };
    const ModeCase modes[] = {{TransitionMode::NextBeat, 1.0},
                              {TransitionMode::NextBar, 4.0},
                              {TransitionMode::NextPhrase, 8.0}};
    for (const auto tempo : tempos)
        for (const auto rate : rates)
            for (const auto blockSize : blocks)
                for (const auto& m : modes)
                {
                    if (blockSize == 1 && m.beats > 1.0)
                        continue; // keep runtime small
                    Scheduler s;
                    const std::int64_t start = 12345;
                    expect("request accepted", s.request(2, m.mode));
                    const auto expected = expectedBoundary(start, tempo, rate, m.beats);
                    const auto got = runUntilSwitch(s, start, blockSize, tempo, rate,
                                                    expected + 2 * blockSize + 1);
                    if (got != expected)
                        std::cerr << "tempo " << tempo << " rate " << rate << " block "
                                  << blockSize << " beats " << m.beats << ": expected "
                                  << expected << " got " << got << '\n';
                    expect("quantized switch sample", got == expected);
                    expect("active after switch", s.activeVariation() == 2 && !s.queuedVariation());
                }

    // Boundary exactly at block start: 120 BPM @ 48 kHz, beat = 24000 = 24 blocks of 1000.
    {
        Scheduler s;
        s.request(1, TransitionMode::NextBeat);
        auto e = s.process(timingAt(23000, 120.0, 48000.0), 1000);
        expect("not before boundary", !e.switched && s.samplesUntilSwitch() == 1000);
        e = s.process(timingAt(24000, 120.0, 48000.0), 1000);
        expect("boundary at block start", e.switched && e.sampleOffset == 0 && e.variation == 1);
    }
    // Boundary inside a block.
    {
        Scheduler s;
        s.request(3, TransitionMode::NextBar);
        const auto e = s.process(timingAt(95900, 120.0, 48000.0), 512);
        expect("boundary inside block", e.switched && e.sampleOffset == 100 && e.variation == 3);
    }
    // Request on an exact boundary fires at offset 0.
    {
        Scheduler s;
        s.request(1, TransitionMode::NextPhrase);
        const auto e = s.process(timingAt(0, 120.0, 48000.0), 256);
        expect("exact boundary request", e.switched && e.sampleOffset == 0);
    }
    // Last sample of a block: boundary at offset blockSize - 1.
    {
        Scheduler s;
        s.request(1, TransitionMode::NextBeat);
        const auto e = s.process(timingAt(24000 - 255, 120.0, 48000.0), 256);
        expect("boundary on last sample", e.switched && e.sampleOffset == 255);
    }

    // Seek / position jumps: boundary is recomputed from the new position.
    {
        Scheduler s;
        s.request(1, TransitionMode::NextBar);
        auto e = s.process(timingAt(50000, 120.0, 48000.0), 512);
        expect("pending before seek", !e.switched && s.samplesUntilSwitch() == 46000);
        e = s.process(timingAt(191990, 120.0, 48000.0), 512); // jump forward near bar 2
        expect("forward seek", e.switched && e.sampleOffset == 10);

        s.request(2, TransitionMode::NextBar);
        e = s.process(timingAt(300000, 120.0, 48000.0), 512);
        expect("pending before backward seek", !e.switched && s.samplesUntilSwitch() == 84000);
        e = s.process(timingAt(95800, 120.0, 48000.0), 512); // jump backward
        expect("backward seek", e.switched && e.sampleOffset == 200 && s.activeVariation() == 2);
    }
    // Host bar alignment via ppq: bar boundary relative to host bars, not sample 0.
    {
        HostTransport host{120.0, 4, 4, true, 5000};
        host.ppqPosition = 3.9;
        host.ppqPositionOfLastBarStart = 0.0;
        const auto t = TransportResolver::resolve(host, PhraseGrid{}, 0, 48000.0);
        Scheduler s;
        s.request(1, TransitionMode::NextBar);
        const auto e = s.process(*t, 4096);
        expect("ppq-aligned bar", t && e.switched && e.sampleOffset == 2400);
    }

    // Stopped transport keeps quantized requests pending.
    {
        Scheduler s;
        s.request(1, TransitionMode::NextBeat);
        auto e = s.process(timingAt(1000, 120.0, 48000.0, false), 512);
        expect("stopped keeps pending", !e.switched && s.queuedVariation() == std::size_t{1}
                                            && !s.samplesUntilSwitch());
        e = s.process(timingAt(23800, 120.0, 48000.0, true), 512);
        expect("fires after play resumes", e.switched && e.sampleOffset == 200);
    }
    // Immediate always fires at offset 0, even when stopped.
    {
        Scheduler s;
        s.request(2, TransitionMode::Immediate);
        expect("immediate until switch", s.samplesUntilSwitch() == 0);
        const auto e = s.process(timingAt(777, 120.0, 48000.0, false), 128);
        expect("immediate at 0", e.switched && e.sampleOffset == 0 && s.activeVariation() == 2);
    }
    // Manual waits for trigger().
    {
        Scheduler s;
        expect("trigger without request", !s.trigger());
        s.request(3, TransitionMode::Manual);
        for (int i = 0; i < 100; ++i)
            expect("manual waits", !s.process(timingAt(i * 512, 120.0, 48000.0), 512).switched);
        expect("manual queued", s.queuedMode() == TransitionMode::Manual && !s.samplesUntilSwitch());
        expect("trigger accepted", s.trigger());
        const auto e = s.process(timingAt(51200, 120.0, 48000.0, false), 512);
        expect("manual fires at 0", e.switched && e.sampleOffset == 0 && s.activeVariation() == 3);
    }
    // Latest request replaces pending; cancel; invalid index; request active.
    {
        Scheduler s;
        s.request(1, TransitionMode::NextBar);
        s.request(2, TransitionMode::NextBeat);
        expect("replaced", s.queuedVariation() == std::size_t{2}
                               && s.queuedMode() == TransitionMode::NextBeat);
        const auto e = s.process(timingAt(1, 120.0, 48000.0), 24000);
        expect("replacement fires", e.switched && e.variation == 2 && e.sampleOffset == 23999);

        s.request(1, TransitionMode::NextBar);
        s.cancel();
        expect("cancelled", !s.queuedVariation() && !s.queuedMode() && !s.samplesUntilSwitch());
        expect("cancelled no switch", !s.process(timingAt(0, 120.0, 48000.0), 512).switched);

        expect("invalid index rejected", !s.request(variationCount, TransitionMode::Immediate));
        s.request(1, TransitionMode::NextBar);
        expect("request active cancels", s.request(2, TransitionMode::NextBar) && !s.queuedVariation());
        expect("zero-length block", !s.process(timingAt(0, 120.0, 48000.0), 0).switched);

        s.reset(3);
        expect("reset", s.activeVariation() == 3 && !s.queuedVariation());
    }

    if (failures == 0)
        std::cout << "SchedulerTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
