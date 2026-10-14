// VF-T02 / VF-T03: engine trace sink is read-only, deterministic across block splits, and truthful.
#include "core/dsp/RhythmEngine.h"
#include "core/dsp/StageGeometry.h"
#include "core/trace/TraceRing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::trace;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}

constexpr double sampleRate = 48000.0;
constexpr double spb = 4800.0;
constexpr std::size_t total = 48000;

std::vector<float> makeInput() {
    std::vector<float> v(total);
    std::uint32_t s = 0x2468ACEu;
    for (auto& x : v) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        x = (static_cast<float>(s & 0xFFFF) / 32768.0f - 1.0f) * 0.5f;
    }
    return v;
}

VariationSettings scene(int which) {
    VariationSettings v;
    auto set = [&](OperationType t, float slice, float depth) { v[t] = {true, depth, slice}; };
    switch (which) {
        case 0: set(OperationType::Repeat, 0.25f, 1.0f); break;
        case 1: set(OperationType::ReverseSlice, 0.5f, 1.0f); break;
        case 2: set(OperationType::Gate, 0.5f, 1.0f); break;
        case 3: set(OperationType::HalfTime, 0.5f, 1.0f); break;
        case 4: set(OperationType::TapeStop, 0.5f, 1.0f); break;
        default:
            set(OperationType::Repeat, 0.25f, 0.8f); set(OperationType::ReverseSlice, 0.5f, 0.6f);
            set(OperationType::Gate, 0.5f, 0.5f); set(OperationType::HalfTime, 1.0f, 0.7f);
            set(OperationType::TapeStop, 1.0f, 0.4f);
    }
    return v;
}

enum class Mode { NoSink, Inactive, Active };

struct Result { std::vector<float> out; std::vector<TraceFrame> frames; std::uint64_t pushed = 0; };

Result run(int which, int block, Mode mode, bool playing = true, std::size_t resetAt = 0) {
    RhythmEngine engine;
    engine.prepare(sampleRate, 1024, 2);
    TraceRing ring;
    if (mode != Mode::NoSink) engine.setTraceSink(&ring);
    ring.setActive(mode == Mode::Active);
    std::vector<float> l = makeInput(), r = l;
    for (std::size_t start = 0; start < total;) {
        std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(block), total - start);
        if (resetAt != 0 && start < resetAt && start + len > resetAt) len = resetAt - start;
        if (resetAt != 0 && start == resetAt) engine.resetHistory();
        float* ch[] = {l.data() + start, r.data() + start};
        engine.process(ch, 2, static_cast<int>(len), scene(which), 1.0f,
                       RhythmTiming{spb, static_cast<double>(start) / spb, playing});
        start += len;
    }
    Result res;
    res.out = l;
    res.pushed = ring.pushedCount();
    res.frames.resize(ringCapacity);
    res.frames.resize(ring.copyLatest(res.frames.data(), res.frames.size()));
    return res;
}

bool sameBits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}
// Frames match exactly except `beat`, which is block-start + n/spb and so differs by rounding (< 1e-9)
// when the block split changes.
bool sameFrames(const std::vector<TraceFrame>& a, const std::vector<TraceFrame>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i].beat - b[i].beat) > 1e-9) return false;
        TraceFrame x = a[i];
        x.beat = b[i].beat;
        if (std::memcmp(&x, &b[i], sizeof(TraceFrame)) != 0) return false;
    }
    return true;
}
} // namespace

int main() {
    // VF-T02: output identical with no sink, an inactive sink and an active sink, for every block size.
    for (int which = 0; which <= 5; ++which) {
        const auto reference = run(which, 128, Mode::NoSink);
        for (int block : {1, 7, 64, 128, 513}) {
            const auto inactive = run(which, block, Mode::Inactive);
            const auto active = run(which, block, Mode::Active);
            expect("inactive sink leaves output bit-identical", sameBits(reference.out, inactive.out));
            expect("active sink leaves output bit-identical", sameBits(reference.out, active.out));
            expect("inactive sink records nothing", inactive.pushed == 0 && inactive.frames.empty());
        }
    }

    // Frames are identical for every block split (cadence is on the absolute engine clock).
    for (int which : {1, 4, 5}) {
        const auto a = run(which, 128, Mode::Active);
        for (int block : {1, 7, 64, 513})
            expect("frames identical across block splits", sameFrames(a.frames, run(which, block, Mode::Active).frames));
    }

    // Cadence and contents.
    {
        const auto res = run(1, 513, Mode::Active); // ReverseSlice, slice 0.5 beat = 2400 samples
        expect("one frame per hop", res.pushed == total / static_cast<std::size_t>(hopSamples));
        bool cadence = !res.frames.empty();
        for (std::size_t k = 0; k < res.frames.size(); ++k) {
            cadence = cadence && res.frames[k].sample % hopSamples == hopSamples - 1
                      && (k == 0 || res.frames[k].sample - res.frames[k - 1].sample == hopSamples);
        }
        expect("frame cadence on absolute clock", cadence);

        // Truth: lag/rate equal StageGeometry's analytic read position once the amount has settled.
        bool truthful = true;
        std::size_t checked = 0;
        for (const auto& f : res.frames) {
            if (f.amount[1] < 1.0f) continue;
            const double t = static_cast<double>(f.sample);
            const auto expected = stage::readPosition(OperationType::ReverseSlice, t, f.beat, 0.5, spb, 0.5 * spb);
            truthful = truthful && std::abs(static_cast<double>(f.lagSamples[1]) - (t - expected.r)) < 1e-2
                       && f.rate[1] == static_cast<float>(expected.rate) && (f.flags & TimingOk) != 0
                       && f.samplesPerBeat == static_cast<float>(spb);
            ++checked;
        }
        expect("reverse lag and rate are truthful", truthful && checked > 100);
        bool inactiveStages = true;
        for (const auto& f : res.frames) inactiveStages = inactiveStages && f.lagSamples[0] == 0.0f && f.lagSamples[2] == 0.0f && f.rate[0] == 1.0f;
        expect("inactive stages report zero lag and unit rate", inactiveStages);
    }
    {
        const auto res = run(2, 128, Mode::Active); // Gate only
        bool gate = !res.frames.empty();
        bool sawClosed = false, sawOpen = false;
        for (const auto& f : res.frames) {
            gate = gate && f.gateGain >= 0.0f && f.gateGain <= 1.0f;
            sawClosed = sawClosed || f.gateGain < 0.05f;
            sawOpen = sawOpen || f.gateGain > 0.95f;
        }
        expect("gate gain measured in range, opens and closes", gate && sawClosed && sawOpen);
    }
    {
        const auto res = run(0, 64, Mode::Active); // Repeat 0.25 beat = 1200 samples; cycle of 4 slices
        bool multiples = true; std::size_t n = 0;
        for (const auto& f : res.frames) {
            if (f.amount[0] < 1.0f) continue;
            const double lag = f.lagSamples[0];
            const double j = std::round(lag / 1200.0);
            multiples = multiples && std::abs(lag - j * 1200.0) < 1e-2 && j >= 0.0 && j <= 3.0;
            ++n;
        }
        expect("repeat lag is a whole number of slices (0..3)", multiples && n > 100);
    }

    // Stopped transport: frames carry no timing and no lag.
    {
        const auto res = run(1, 128, Mode::Active, false);
        bool idle = !res.frames.empty();
        for (const auto& f : res.frames)
            idle = idle && (f.flags & TimingOk) == 0 && f.lagSamples[1] == 0.0f && f.samplesPerBeat == 0.0f && f.beat == 0.0;
        expect("stopped transport yields idle frames", idle);
    }

    // Epoch: resetHistory starts a new epoch and the engine clock restarts.
    {
        const auto res = run(1, 128, Mode::Active, true, 24000);
        expect("epoch increments after resetHistory", !res.frames.empty() && res.frames.front().epoch != res.frames.back().epoch);
        const auto start = latestEpochStart(res.frames.data(), res.frames.size());
        expect("latest epoch start is after reset", start > 0 && start < res.frames.size());
        expect("engine clock restarts at reset", res.frames[start].sample == hopSamples - 1);
    }

    if (failures == 0) std::cout << "TraceEngineTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
