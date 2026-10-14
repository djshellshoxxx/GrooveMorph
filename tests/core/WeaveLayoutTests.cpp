// VF-T06: Warp Weave geometry from real engine frames. Each effect must produce its documented shape,
// and both timelines share one scale (lag in beats == (xTop - xBottom) * (span + lead)).
#include "core/dsp/RhythmEngine.h"
#include "core/trace/TraceRing.h"
#include "core/viz/WeaveLayout.h"

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
constexpr std::size_t total = 96000; // 20 beats

std::vector<TraceFrame> frames(const VariationSettings& v) {
    RhythmEngine engine;
    engine.prepare(sampleRate, 1024, 1);
    TraceRing ring;
    ring.setActive(true);
    engine.setTraceSink(&ring);
    std::vector<float> x(total, 0.1f);
    for (std::size_t start = 0; start < total; start += 256) {
        float* ch[] = {x.data() + start};
        engine.process(ch, 1, 256, v, 1.0f, RhythmTiming{spb, static_cast<double>(start) / spb, true});
    }
    std::vector<TraceFrame> f(ringCapacity);
    f.resize(ring.copyLatest(f.data(), f.size()));
    return f;
}

VariationSettings one(OperationType t, float slice) {
    VariationSettings v;
    v[t] = {true, 1.0f, slice};
    return v;
}

constexpr WeaveParams params{12.0, 4.0, 2.0, 200, 19.9};
constexpr double scale = params.spanBeats + params.leadBeats;

// Threads within one frame hop (about 0.03 beat here) of a stage discontinuity are only accurate to the
// frame cadence, which is also where the engine crossfades; shape checks skip that margin.
bool nearBoundary(double beat, double cell) { const double x = beat / cell; return std::fabs(x - std::round(x)) * cell < 0.03; }

double topBeat(const Thread& t) { return params.viewStartBeat - params.leadBeats + t.xTop * scale; }
double lagBeats(const Thread& t) { return (t.xTop - t.xBottom) * scale; }

std::vector<Thread> weave(const std::vector<TraceFrame>& f, std::size_t cap = 256) {
    std::vector<Thread> out(cap);
    out.resize(buildWeave(FrameWindow{f.data(), f.size()}, params, out.data(), out.size()));
    return out;
}
} // namespace

int main() {
    // Live: vertical threads.
    {
        const auto t = weave(frames(VariationSettings{}));
        bool vertical = t.size() == 200;
        for (const auto& th : t)
            vertical = vertical && std::abs(th.xTop - th.xBottom) < 1e-4f && th.primary == Effect::Live && !th.noData && !th.planned;
        expect("live threads are vertical", vertical);
        expect("threads span the view", !t.empty() && topBeat(t.front()) > 12.0 && topBeat(t.back()) < 16.0);
    }

    // Repeat (slice 0.25 beat, cycle of 4 slices = 1 beat): lag = slot * 0.25 beat (fan-in).
    {
        const auto t = weave(frames(one(OperationType::Repeat, 0.25f)));
        bool fan = !t.empty();
        for (const auto& th : t) {
            const double within = std::fmod(topBeat(th), 1.0);
            const double slot = std::min(3.0, std::floor(within / 0.25));
            // skip threads within 1/200 of a slice boundary (frame granularity)
            const double toBoundary = std::fabs(within / 0.25 - std::round(within / 0.25)) * 0.25;
            if (toBoundary < 0.03) continue;
            fan = fan && std::abs(lagBeats(th) - slot * 0.25) < 0.02 && th.primary == (slot > 0.0 ? Effect::Repeat : Effect::Live);
        }
        expect("repeat fans in by whole slices", fan);
    }

    // Reverse (slice 0.5 beat): source beat decreases while output beat increases, within a slice.
    {
        const auto t = weave(frames(one(OperationType::ReverseSlice, 0.5f)));
        std::size_t checked = 0;
        bool twist = true;
        for (std::size_t i = 1; i < t.size(); ++i) {
            if (std::floor(topBeat(t[i]) / 0.5) != std::floor(topBeat(t[i - 1]) / 0.5)) continue; // same slice
            if (t[i].noData || t[i - 1].noData) continue;
            if (nearBoundary(topBeat(t[i]), 0.5) || nearBoundary(topBeat(t[i - 1]), 0.5)) continue;
            twist = twist && t[i].xBottom < t[i - 1].xBottom && t[i].primary == Effect::Reverse;
            ++checked;
        }
        expect("reverse threads cross (source runs backwards)", twist && checked > 100);
        // lag = 2p + 1 sample: at slice position p (beats) the lag is 2p + 1/spb beats (shared scale)
        bool lagOk = true;
        for (const auto& th : t) {
            const double p = topBeat(th) - std::floor(topBeat(th) / 0.5) * 0.5;
            if (p < 0.03 || p > 0.47) continue;
            lagOk = lagOk && std::abs(lagBeats(th) - (2.0 * p + 1.0 / spb)) < 0.02;
        }
        expect("reverse lag equals 2p on the shared scale", lagOk);
    }

    // HalfTime (slice 0.5): source advances at half speed.
    {
        const auto t = weave(frames(one(OperationType::HalfTime, 0.5f)));
        bool slope = true; std::size_t checked = 0;
        for (std::size_t i = 3; i < t.size(); ++i) {
            if (std::floor(topBeat(t[i]) / 0.5) != std::floor(topBeat(t[i - 3]) / 0.5)) continue;
            if (nearBoundary(topBeat(t[i]), 0.5) || nearBoundary(topBeat(t[i - 3]), 0.5)) continue;
            const double dTop = t[i].xTop - t[i - 3].xTop, dBot = t[i].xBottom - t[i - 3].xBottom;
            slope = slope && std::abs(dBot / dTop - 0.5) < 0.05;
            ++checked;
        }
        expect("half-time threads advance at half speed", slope && checked > 100);
    }

    // TapeStop (slice 0.5): slope falls from 1 towards 0.
    {
        const auto t = weave(frames(one(OperationType::TapeStop, 0.5f)));
        double early = -1.0, late = -1.0;
        for (std::size_t i = 3; i < t.size(); ++i) {
            const double within = topBeat(t[i]) - std::floor(topBeat(t[i]) / 0.5) * 0.5;
            const double dTop = t[i].xTop - t[i - 3].xTop, dBot = t[i].xBottom - t[i - 3].xBottom;
            if (std::floor(topBeat(t[i]) / 0.5) != std::floor(topBeat(t[i - 3]) / 0.5)) continue;
            if (within > 0.06 && within < 0.1 && early < 0.0) early = dBot / dTop;
            if (within > 0.4 && within < 0.45) late = dBot / dTop;
        }
        expect("tape-stop decelerates", early > 0.7 && late >= 0.0 && late < 0.35 && early > late + 0.4);
    }

    // Gate (slice 0.5): closed threads in the second half of each slice, dimmer and flagged.
    {
        const auto t = weave(frames(one(OperationType::Gate, 0.5f)));
        std::size_t closed = 0, open = 0; bool dim = true, positionsOk = true;
        float openOpacity = 0.0f;
        for (const auto& th : t) {
            const double within = topBeat(th) - std::floor(topBeat(th) / 0.5) * 0.5;
            if (within > 0.28 && within < 0.48) { positionsOk = positionsOk && th.gateClosed; ++closed; dim = dim && th.opacity < 0.3f; }
            if (within > 0.02 && within < 0.22) { positionsOk = positionsOk && !th.gateClosed; ++open; openOpacity = std::max(openOpacity, th.opacity); }
            positionsOk = positionsOk && std::abs(th.xTop - th.xBottom) < 1e-4f; // gate never moves the source
        }
        expect("gate flags the closed half", positionsOk && closed > 20 && open > 20);
        expect("closed threads are dimmer than open ones", dim && openOpacity > 0.7f);
    }

    // Capacity, ink front and no-data.
    {
        const auto f = frames(one(OperationType::Repeat, 0.25f));
        expect("capacity caps the thread count", weave(f, 37).size() == 37);
        WeaveParams half = params; half.nowBeat = 14.0; // ink front mid-view: planned zone is not drawn here (S5)
        std::vector<Thread> out(256);
        out.resize(buildWeave(FrameWindow{f.data(), f.size()}, half, out.data(), out.size()));
        expect("only heard threads before the ink front", out.size() == 100);
        WeaveParams early = params; early.viewStartBeat = 1.0; early.nowBeat = 5.0; // before most frames were recorded? still inside
        out.resize(256);
        out.resize(buildWeave(FrameWindow{}, early, out.data(), out.size()));
        expect("empty window yields no-data threads", !out.empty() && std::all_of(out.begin(), out.end(), [](const Thread& t) { return t.noData; }));
        expect("zero capacity", buildWeave(FrameWindow{f.data(), f.size()}, params, nullptr, 0) == 0);
    }

    if (failures == 0) std::cout << "WeaveLayoutTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
