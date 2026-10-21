#include "core/dsp/DemoSource.h"
#include "core/dsp/LoopSource.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace groovemorph::core;

namespace
{
int failures = 0;
void expect(const std::string& name, bool ok)
{
    if (!ok)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

// Runs a capture of `bars` bars at 48 kHz in 480-sample blocks. tempoAt(blockIndex) gives samples per beat,
// meterAt(blockIndex) the meter. Returns the final state.
template <class Tempo, class Meter>
LoopCapture::State run(LoopCapture& cap, int bars, int meter0, Tempo tempoAt, Meter meterAt, int blocks)
{
    cap.prepare(48000.0);
    cap.arm(bars, meter0);
    std::vector<float> l(480, 0.25f), r(480, -0.25f);
    const float* ch[2] = {l.data(), r.data()};
    double beat = 0.1; // starts mid-beat: recording waits for the next bar line
    for (int b = 0; b < blocks && cap.state() != LoopCapture::State::Done && cap.state() != LoopCapture::State::Idle; ++b)
    {
        const double spb = tempoAt(b);
        cap.process(ch, 2, 480, beat, spb, true, meterAt(b));
        beat += 480.0 / spb;
    }
    return cap.state();
}
} // namespace

int main()
{
    const auto steady = [](int) { return 24000.0; }; // 120 BPM
    const auto meter4 = [](int) { return 4; };

    // CP-1 constant tempo and meter completes.
    {
        LoopCapture cap;
        expect("CP-1 steady completes", run(cap, 1, 4, steady, meter4, 2000) == LoopCapture::State::Done);
        expect("CP-1 steady length", cap.recordedSamples() == 4 * 24000 && cap.failure() == 0);
    }
    // Tiny drift within 0.1 % is tolerated.
    {
        LoopCapture cap;
        const auto jitter = [](int b) { return 24000.0 * (1.0 + ((b & 1) ? 0.0004 : -0.0004)); };
        expect("CP-1 jitter below tolerance completes", run(cap, 1, 4, jitter, meter4, 2000) == LoopCapture::State::Done);
    }
    // Tempo change while recording aborts with failure 3.
    {
        LoopCapture cap;
        const auto change = [](int b) { return b < 250 ? 24000.0 : 22000.0; };
        expect("CP-1 tempo change aborts", run(cap, 2, 4, change, meter4, 3000) == LoopCapture::State::Idle);
        expect("CP-1 tempo change reason 3", cap.failure() == 3);
    }
    // Slow tempo ramp accumulates past the tolerance and aborts.
    {
        LoopCapture cap;
        const auto ramp = [](int b) { return 24000.0 - 5.0 * b; };
        expect("CP-1 ramp aborts", run(cap, 2, 4, ramp, meter4, 3000) == LoopCapture::State::Idle && cap.failure() == 3);
    }
    // Meter change aborts with failure 4 (also while still waiting for the bar line).
    {
        LoopCapture cap;
        const auto change = [](int b) { return b < 250 ? 4 : 3; };
        expect("CP-1 meter change aborts", run(cap, 2, 4, steady, change, 3000) == LoopCapture::State::Idle && cap.failure() == 4);
        LoopCapture early;
        const auto now3 = [](int) { return 3; };
        expect("CP-1 meter change before start aborts", run(early, 1, 4, steady, now3, 10) == LoopCapture::State::Idle && early.failure() == 4);
    }
    // Meter 0 (unknown) keeps the old behaviour.
    {
        LoopCapture cap;
        const auto unknown = [](int) { return 0; };
        expect("CP-1 unknown meter ignored", run(cap, 1, 4, steady, unknown, 2000) == LoopCapture::State::Done);
    }
    // A finished capture stays readable until released (the processor copies it before publishing).
    {
        LoopCapture cap;
        run(cap, 1, 4, steady, meter4, 2000);
        expect("CP-1 done keeps data", cap.state() == LoopCapture::State::Done && cap.channel(0)[10] == 0.25f && cap.channel(1)[10] == -0.25f);
        cap.setFailure(5);
        expect("CP-1 failure 5 can be reported without losing the capture", cap.failure() == 5 && cap.state() == LoopCapture::State::Done);
        cap.release();
    }

    // CP-2 demo: default meter is bit-identical to the explicit 4/4; other meters place the snare per the rule.
    {
        const int n = 48000 * 4;
        std::vector<float> a(n), b(n), c(n);
        float* pa[1] = {a.data()};
        float* pb[1] = {b.data()};
        DemoSource::render(pa, 1, n, 0.0, 24000.0, 48000.0);
        DemoSource::render(pb, 1, n, 0.0, 24000.0, 48000.0, 4);
        expect("CP-2 default meter identical to 4/4", a == b);
        expect("CP-2 sanity: demo not silent", std::abs(a[100]) > 0.01f);
        // Energy of the noisy snare window (60..160 ms after the beat) per beat, relative to the kick-only beat.
        auto snareEnergy = [&](const std::vector<float>& x, int beat) {
            double e = 0;
            const int start = beat * 24000 + 1200, end = beat * 24000 + 9000;
            for (int i = start; i < end && i < static_cast<int>(x.size()); ++i)
            {
                // First difference: the kick is a low sine, the snare is noise, so this isolates the snare.
                const double d = static_cast<double>(x[static_cast<std::size_t>(i)]) - x[static_cast<std::size_t>(i - 1)];
                e += d * d;
            }
            return e;
        };
        auto beatsWithSnare = [&](int meter) {
            std::vector<float> x(n);
            float* p[1] = {x.data()};
            DemoSource::render(p, 1, n, 0.0, 24000.0, 48000.0, meter);
            std::vector<int> hits;
            const double base = snareEnergy(x, 0);
            for (int beat = 0; beat < meter && beat < 8; ++beat)
                if (snareEnergy(x, beat) > 10.0 * base + 1e-9)
                    hits.push_back(beat);
            return hits;
        };
        expect("CP-2 4/4 snare on 2 and 4", beatsWithSnare(4) == std::vector<int>({1, 3}));
        expect("CP-2 3/4 snare on 2 only", beatsWithSnare(3) == std::vector<int>({1}));
        expect("CP-2 6/8 snare on 2, 4, 6", beatsWithSnare(6) == std::vector<int>({1, 3, 5}));
        expect("CP-2 5 beats snare on 2 and 4", beatsWithSnare(5) == std::vector<int>({1, 3}));
        // Later bars repeat the pattern: beat 4 of a 3/4 bar structure (beat index 4 = bar 2 beat 2) has a snare.
        std::vector<float> x(n);
        float* p[1] = {x.data()};
        DemoSource::render(p, 1, n, 0.0, 24000.0, 48000.0, 3);
        expect("CP-2 3/4 second bar repeats", snareEnergy(x, 4) > 10.0 * snareEnergy(x, 3));
        // A meter change mid-stream is honoured from the next block on (stateless: pure function of position).
        DemoSource::render(p, 1, n, 0.0, 24000.0, 48000.0, 99);
        std::vector<float> y(n);
        float* py[1] = {y.data()};
        DemoSource::render(py, 1, n, 0.0, 24000.0, 48000.0, 4);
        expect("CP-2 out-of-range meter falls back to 4", x == y);
    }

    if (failures == 0)
        std::cout << "LoopCaptureHardeningTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
