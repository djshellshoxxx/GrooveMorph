// VF-T05: the planner (future, from scene settings) must agree with what the engine actually did
// (past, from trace frames) when both use the same settings.
#include "core/dsp/RhythmEngine.h"
#include "core/trace/TraceRing.h"
#include "core/viz/PlannedPath.h"
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

std::vector<TraceFrame> frames(const VariationSettings& v, float intensity, const std::array<double, operationCount>& phase) {
    RhythmEngine engine;
    engine.prepare(sampleRate, 1024, 1);
    TraceRing ring;
    ring.setActive(true);
    engine.setTraceSink(&ring);
    std::vector<float> x(total, 0.1f);
    for (std::size_t start = 0; start < total; start += 256) {
        float* ch[] = {x.data() + start};
        RhythmTiming timing{spb, static_cast<double>(start) / spb, true};
        timing.phaseOffsetBeats = phase;
        engine.process(ch, 1, 256, v, intensity, timing);
    }
    std::vector<TraceFrame> f(ringCapacity);
    f.resize(ring.copyLatest(f.data(), f.size()));
    return f;
}

struct Agreement { std::size_t compared = 0, agree = 0, weightOk = 0, maskOk = 0; };

Agreement compare(const VariationSettings& v, float intensity, const std::array<double, operationCount>& phase = {}) {
    const auto f = frames(v, intensity, phase);
    const FrameWindow w{f.data(), f.size()};
    PlanSegment seg{0.0, 1000.0, v};
    PlanContext ctx;
    ctx.segments = &seg;
    ctx.segmentCount = 1;
    ctx.intensity = intensity;
    ctx.samplesPerBeat = spb;
    ctx.historySamples = static_cast<std::size_t>(std::ceil(RhythmEngine::maxHistorySeconds * sampleRate));
    ctx.phaseOffsetBeats = phase;
    Agreement a;
    for (double beat = 12.0; beat < 19.5; beat += 0.011) {
        const auto sample = sampleAtBeat(w, beat);
        if (!sample) continue;
        const auto heard = resolveThread(w, *sample);
        const auto heardBeat = heard.noData ? std::nullopt : beatAtSample(w, heard.sourceSample);
        const auto planned = planThread(ctx, beat);
        if (!heardBeat || !planned.valid) continue;
        ++a.compared;
        if (std::abs(*heardBeat - planned.sourceBeat) < 0.01) ++a.agree;
        if (std::abs(heard.weight - planned.weight) < 0.05f) ++a.weightOk;
        if (heard.wetMask == planned.wetMask) ++a.maskOk;
    }
    return a;
}

VariationSettings one(OperationType t, float slice, float depth = 1.0f) {
    VariationSettings v;
    v[t] = {true, depth, slice};
    return v;
}
} // namespace

int main() {
    struct Case { const char* name; VariationSettings v; float intensity; double minAgree; };
    VariationSettings chain;
    chain[OperationType::Repeat] = {true, 1.0f, 0.25f};
    chain[OperationType::ReverseSlice] = {true, 1.0f, 0.5f};
    VariationSettings all;
    all[OperationType::Repeat] = {true, 0.8f, 0.25f}; all[OperationType::ReverseSlice] = {true, 0.6f, 0.5f};
    all[OperationType::Gate] = {true, 0.5f, 0.5f}; all[OperationType::HalfTime] = {true, 0.7f, 1.0f};
    all[OperationType::TapeStop] = {true, 0.4f, 1.0f};
    const Case cases[] = {
        {"repeat", one(OperationType::Repeat, 0.25f), 1.0f, 0.95},
        {"reverse", one(OperationType::ReverseSlice, 0.5f), 1.0f, 0.95},
        {"half-time", one(OperationType::HalfTime, 0.5f), 1.0f, 0.95},
        {"tape-stop", one(OperationType::TapeStop, 0.5f), 1.0f, 0.95},
        {"gate", one(OperationType::Gate, 0.5f), 1.0f, 0.99},
        {"repeat+reverse", chain, 1.0f, 0.85},
        {"all five", all, 1.0f, 0.80},
        {"low intensity", one(OperationType::Repeat, 0.25f), 0.3f, 0.99},
    };
    for (const auto& c : cases) {
        const auto a = compare(c.v, c.intensity);
        const double frac = a.compared ? static_cast<double>(a.agree) / static_cast<double>(a.compared) : 0.0;
        expect((std::string(c.name) + ": enough samples").c_str(), a.compared > 400);
        std::cout << c.name << ": compared " << a.compared << ", source agreement " << frac << ", weight agreement " << (a.compared ? static_cast<double>(a.weightOk) / static_cast<double>(a.compared) : 0.0) << "\n";
        expect((std::string(c.name) + ": planned source agrees with the engine").c_str(), frac >= c.minAgree);
        expect((std::string(c.name) + ": planned weight agrees").c_str(), a.weightOk >= a.compared * 9 / 10);
    }

    // Phase offsets are honoured the same way.
    {
        std::array<double, operationCount> phase{};
        phase[static_cast<std::size_t>(OperationType::ReverseSlice)] = 0.13;
        const auto a = compare(one(OperationType::ReverseSlice, 0.5f), 1.0f, phase);
        expect("phase offset agrees", a.compared > 400 && a.agree >= a.compared * 95 / 100);
    }

    // Segments: settings change at a beat; outside every segment the plan is invalid.
    {
        PlanSegment segs[2] = {{0.0, 4.0, VariationSettings{}}, {4.0, 8.0, one(OperationType::Repeat, 0.25f)}};
        PlanContext ctx;
        ctx.segments = segs; ctx.segmentCount = 2; ctx.intensity = 1.0f; ctx.samplesPerBeat = spb;
        ctx.historySamples = 192000;
        const auto live = planThread(ctx, 2.0);
        expect("segment without effects is live", live.valid && live.sourceBeat == 2.0 && live.wetMask == 0 && live.weight == 1.0f);
        const auto rep = planThread(ctx, 5.6);
        expect("segment with repeat fans in", rep.valid && rep.sourceBeat < 5.6 && (rep.wetMask & 1u));
        expect("outside every segment is invalid", !planThread(ctx, 9.0).valid);
        expect("no segments is invalid", !planThread(PlanContext{}, 1.0).valid);
    }

    // buildPlannedWeave: only future threads, flagged planned, tentative after the boundary.
    {
        PlanSegment seg{0.0, 1000.0, one(OperationType::Repeat, 0.25f)};
        PlanContext ctx;
        ctx.segments = &seg; ctx.segmentCount = 1; ctx.intensity = 1.0f; ctx.samplesPerBeat = spb; ctx.historySamples = 192000;
        WeaveParams p{12.0, 4.0, 2.0, 200, 14.0};
        std::vector<Thread> out(256);
        out.resize(buildPlannedWeave(ctx, p, 15.0, out.data(), out.size()));
        expect("planned threads only right of the ink front", out.size() == 100);
        bool flags = !out.empty();
        for (const auto& t : out) flags = flags && t.planned && !t.noData && t.opacity <= 0.85f * 0.6f + 1e-6f;
        expect("planned threads are flagged and dimmed", flags);
        const auto firstTentative = std::find_if(out.begin(), out.end(), [](const Thread& t) { return t.tentative; });
        expect("tentative starts at the boundary beat", firstTentative != out.end() && firstTentative > out.begin() && firstTentative < out.end());
        expect("zero capacity", buildPlannedWeave(ctx, p, 15.0, nullptr, 0) == 0);
    }

    if (failures == 0) std::cout << "PlannedPathTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
