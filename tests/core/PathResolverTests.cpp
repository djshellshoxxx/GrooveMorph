// VF-T04: PathResolver maps heard output moments back to the input moments they came from.
// Ground truth comes from the real engine: an impulse is fed through it and every place it reappears
// in the output must resolve to the impulse's original sample.
#include "core/dsp/RhythmEngine.h"
#include "core/trace/TraceRing.h"
#include "core/viz/PathResolver.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::trace;
using namespace groovemorph::core::viz;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}

constexpr double sampleRate = 48000.0;
constexpr double spb = 4800.0;
constexpr std::size_t total = 96000;
// Mid-slice and in slot 0 of a Repeat cycle (cycle length 4800 samples): never on a slice boundary.
constexpr std::size_t impulseAt = 29300;

struct Run { std::vector<float> out; std::vector<TraceFrame> frames; };

Run run(const VariationSettings& v, float intensity = 1.0f, bool playing = true) {
    RhythmEngine engine;
    engine.prepare(sampleRate, 1024, 1);
    TraceRing ring;
    ring.setActive(true);
    engine.setTraceSink(&ring);
    std::vector<float> x(total, 0.0f);
    x[impulseAt] = 1.0f;
    for (std::size_t start = 0; start < total; start += 256) {
        float* ch[] = {x.data() + start};
        engine.process(ch, 1, 256, v, intensity, RhythmTiming{spb, static_cast<double>(start) / spb, playing});
    }
    Run r;
    r.out = x;
    r.frames.resize(ringCapacity);
    r.frames.resize(ring.copyLatest(r.frames.data(), r.frames.size()));
    return r;
}

// Every output sample carrying (most of) the impulse must resolve to the impulse's source sample.
// Returns the number of strong output samples found; `bad` counts mismatches.
std::size_t checkImpulse(const Run& r, int& bad, std::size_t from = impulseAt) {
    FrameWindow w{r.frames.data(), r.frames.size()};
    std::size_t strong = 0;
    for (std::size_t t = from; t < r.out.size() - 400; ++t) {
        if (std::abs(r.out[t]) < 0.9f) continue;
        ++strong;
        const auto th = resolveThread(w, static_cast<double>(t));
        if (th.noData || std::abs(th.sourceSample - static_cast<double>(impulseAt)) > 0.5) ++bad;
    }
    return strong;
}

VariationSettings one(OperationType t, float slice) {
    VariationSettings v;
    v[t] = {true, 1.0f, slice};
    return v;
}
} // namespace

int main() {
    // Window helpers.
    {
        std::vector<TraceFrame> f(3);
        for (int i = 0; i < 3; ++i) { f[i].sample = 127 + i * 128; f[i].epoch = 1; f[i].flags = TimingOk; f[i].beat = 10.0 + i * 128.0 / 4800.0; f[i].samplesPerBeat = 4800.0f; }
        FrameWindow w{f.data(), f.size()};
        expect("nearest exact", nearestFrame(w, 255.0) == 1);
        expect("nearest rounds", nearestFrame(w, 300.0) == 1 && nearestFrame(w, 330.0) == 2);
        expect("nearest outside window", nearestFrame(w, 5000.0) == noFrame && nearestFrame(w, -500.0) == noFrame);
        expect("empty window", nearestFrame(FrameWindow{}, 0.0) == noFrame);
        const auto b = beatAtSample(w, 255.0 + 50.0);
        expect("beat at sample extrapolates by samplesPerBeat", b && std::abs(*b - (10.0 + 128.0 / 4800.0 + 50.0 / 4800.0)) < 1e-9);
        expect("no extrapolation beyond one hop", !beatAtSample(w, 255.0 + 480.0));
        const auto s = sampleAtBeat(w, 10.0 + 128.0 / 4800.0);
        expect("sample at beat", s && std::abs(*s - 255.0) < 1e-6);
        f[1].flags = 0;
        expect("no timing, no beat", !beatAtSample(w, 255.0));
    }

    // Single stages (integer-lag effects): the impulse reappears at its input sample.
    for (auto [name, v] : {std::pair<const char*, VariationSettings>{"repeat", one(OperationType::Repeat, 0.25f)},
                           {"reverse", one(OperationType::ReverseSlice, 0.5f)}}) {
        const auto r = run(v);
        int bad = 0;
        const auto strong = checkImpulse(r, bad);
        expect((std::string(name) + " impulse reappears").c_str(), strong >= (std::string(name) == "repeat" ? 3u : 1u));
        expect((std::string(name) + " resolves to its source").c_str(), bad == 0);
    }

    // Composed chain Repeat -> Reverse: lags compose through the chain.
    {
        VariationSettings v;
        v[OperationType::Repeat] = {true, 1.0f, 0.25f};
        v[OperationType::ReverseSlice] = {true, 1.0f, 0.5f};
        const auto r = run(v);
        int bad = 0;
        const auto strong = checkImpulse(r, bad);
        expect("composed chain impulse reappears", strong >= 1);
        expect("composed chain resolves to source", bad == 0);
    }

    // Live path: no effects, intensity 1: output sample == source sample, full weight.
    {
        const auto r = run(VariationSettings{});
        FrameWindow w{r.frames.data(), r.frames.size()};
        const auto th = resolveThread(w, 50000.0);
        expect("live thread", !th.noData && th.sourceSample == 50000.0 && th.weight == 1.0f && th.wetMask == 0);
    }

    // Intensity below 0.5 is drawn as the live path even when effects are enabled.
    {
        const auto r = run(one(OperationType::Repeat, 0.25f), 0.3f);
        FrameWindow w{r.frames.data(), r.frames.size()};
        bool live = true;
        for (double t = 40000.0; t < 60000.0; t += 777.0) {
            const auto th = resolveThread(w, t);
            live = live && !th.noData && th.sourceSample == t && th.wetMask == 0;
        }
        expect("low intensity resolves live", live);
    }

    // Gate lowers weight when closed, never moves the source.
    {
        const auto r = run(one(OperationType::Gate, 0.5f));
        FrameWindow w{r.frames.data(), r.frames.size()};
        float minW = 1.0f, maxW = 0.0f; bool moved = false;
        for (double t = 40000.0; t < 60000.0; t += 61.0) {
            const auto th = resolveThread(w, t);
            minW = std::min(minW, th.weight); maxW = std::max(maxW, th.weight);
            moved = moved || th.sourceSample != t;
        }
        expect("gate dims the closed part", minW < 0.1f && maxW > 0.9f);
        expect("gate does not move the source", !moved);
    }

    // Dominant-branch rule at the 0.5 boundary and weight = max(a, 1-a).
    {
        std::vector<TraceFrame> f(8);
        for (std::size_t i = 0; i < f.size(); ++i) { f[i].sample = static_cast<std::int64_t>(127 + 128 * i); f[i].epoch = 1; f[i].flags = TimingOk; f[i].beat = static_cast<double>(f[i].sample) / 4800.0; f[i].samplesPerBeat = 4800.0f; f[i].intensity = 1.0f; f[i].lagSamples[0] = 128.0f; }
        FrameWindow w{f.data(), f.size()};
        f[7].amount[0] = 0.49f; auto lo = resolveThread(w, 127.0 + 7 * 128);
        f[7].amount[0] = 0.50f; auto mid = resolveThread(w, 127.0 + 7 * 128);
        f[7].amount[0] = 0.51f; auto hi = resolveThread(w, 127.0 + 7 * 128);
        expect("below 0.5 stays dry", lo.sourceSample == 127.0 + 7 * 128 && lo.wetMask == 0 && std::abs(lo.weight - 0.51f) < 1e-6f);
        expect("0.5 takes the wet branch", mid.sourceSample == 127.0 + 7 * 128 - 128.0 && (mid.wetMask & 1u) && std::abs(mid.weight - 0.5f) < 1e-6f);
        expect("above 0.5 wet, weight 0.51", hi.sourceSample == 127.0 + 7 * 128 - 128.0 && std::abs(hi.weight - 0.51f) < 1e-6f);
    }

    // NoData: before the window, and when the chain walks off the start of the window.
    {
        const auto r = run(one(OperationType::Repeat, 0.25f));
        FrameWindow w{r.frames.data(), r.frames.size()};
        expect("sample before window is NoData", resolveThread(w, -1000.0).noData);
        expect("sample after window is NoData", resolveThread(w, 1.0e9).noData);
        std::vector<TraceFrame> few(r.frames.begin() + 400, r.frames.begin() + 420);
        FrameWindow tiny{few.data(), few.size()};
        bool anyNoData = false;
        for (double t = static_cast<double>(few.front().sample); t <= static_cast<double>(few.back().sample); t += 128.0) {
            const auto th = resolveThread(tiny, t);
            anyNoData = anyNoData || th.noData;
        }
        expect("walking past the window start is NoData", anyNoData);
    }

    // Stopped transport: weight 0, no movement.
    {
        const auto r = run(one(OperationType::Repeat, 0.25f), 1.0f, false);
        FrameWindow w{r.frames.data(), r.frames.size()};
        const auto th = resolveThread(w, 50000.0);
        expect("stopped transport has zero weight", !th.noData && th.weight == 0.0f && th.wetMask == 0);
    }

    // Monotonic: the source never lies after the output.
    {
        VariationSettings v;
        v[OperationType::Repeat] = {true, 1.0f, 0.25f}; v[OperationType::ReverseSlice] = {true, 0.7f, 0.5f};
        v[OperationType::HalfTime] = {true, 1.0f, 1.0f}; v[OperationType::TapeStop] = {true, 1.0f, 1.0f};
        const auto r = run(v);
        FrameWindow w{r.frames.data(), r.frames.size()};
        bool ok = true;
        for (double t = 20000.0; t < 90000.0; t += 97.0) {
            const auto th = resolveThread(w, t);
            if (!th.noData) ok = ok && th.sourceSample <= t + 1e-9 && th.weight >= 0.0f && th.weight <= 1.0f;
        }
        expect("source never after output; weight in [0,1]", ok);
    }

    if (failures == 0) std::cout << "PathResolverTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
