// The plan builder must reproduce the processor's placement gating: planned threads built from a scene
// with placement and event lanes must match what the engine did when driven the way the processor drives it.
#include "core/control/PlacementEvaluator.h"
#include "core/dsp/RhythmEngine.h"
#include "core/trace/TraceRing.h"
#include "core/viz/PathResolver.h"
#include "core/viz/PlanBuilder.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::trace;
using namespace groovemorph::core::viz;
namespace ctl = groovemorph::core::control;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}

constexpr double sampleRate = 48000.0;
constexpr double spb = 4800.0;
constexpr std::size_t total = 192000; // 40 beats
const ctl::GridContext grid{4, 4};

// Drives the engine exactly like PluginProcessor's segment loop does for placement.
std::vector<TraceFrame> processorLikeRun(const VariationSettings& scene) {
    RhythmEngine engine;
    engine.prepare(sampleRate, 1024, 1);
    TraceRing ring;
    ring.setActive(true);
    engine.setTraceSink(&ring);
    std::uint32_t enabledMask = 0;
    for (std::size_t i = 0; i < operationCount; ++i) if (scene.operations[i].enabled) enabledMask |= 1u << i;
    std::vector<float> x(total, 0.1f);
    const std::size_t maxBlock = 256;
    for (std::size_t done = 0; done < total;) {
        int n = static_cast<int>(std::min(maxBlock, total - done));
        RhythmTiming rhythm{spb, static_cast<double>(done) / spb, true};
        VariationSettings gated;
        const VariationSettings* render = &scene;
        if (ctl::isActive(scene.control)) {
            const auto t = ctl::evaluate(scene.control, grid, rhythm.beatAtBlockStart, enabledMask);
            if (t.gated) {
                gated = scene;
                for (std::size_t i = 0; i < operationCount; ++i) {
                    if (!t.active[i]) gated.operations[i].enabled = false;
                    else {
                        gated.operations[i].depth *= t.depthScale[i];
                        if (t.sliceBeats[i] > 0.0f) gated.operations[i].sliceBeats = t.sliceBeats[i];
                        rhythm.phaseOffsetBeats[i] = t.phaseOffsetBeats[i];
                    }
                }
                render = &gated;
                const double samples = t.ticksUntilChange / ctl::ticksPerBeat * spb;
                if (std::isfinite(samples) && samples < static_cast<double>(n)) n = std::max(1, static_cast<int>(std::llround(samples)));
            }
        }
        float* ch[] = {x.data() + done};
        engine.process(ch, 1, n, *render, 1.0f, rhythm);
        done += static_cast<std::size_t>(n);
    }
    std::vector<TraceFrame> f(ringCapacity);
    f.resize(ring.copyLatest(f.data(), f.size()));
    return f;
}

VariationSettings placed() {
    VariationSettings v;
    v[OperationType::Repeat] = {true, 1.0f, 0.25f};
    v[OperationType::ReverseSlice] = {true, 1.0f, 0.5f};
    v.control.placement.enabled = true;
    v.control.placement.window = ctl::WindowMode::Custom;
    v.control.placement.startPhase = 0.5f; // second half of the 4-bar phrase
    v.control.placement.spanPhase = 0.5f;
    return v;
}
} // namespace

int main() {
    // Inert control: a single segment spanning the range.
    {
        VariationSettings v;
        PlanSegment seg[8];
        const auto n = buildPlanSegments(v, grid, 2.0, 10.0, seg, 8);
        expect("inert scene is one segment", n == 1 && seg[0].startBeat == 2.0 && seg[0].endBeat == 10.0);
    }
    // Invalid input and capacity.
    {
        PlanSegment seg[2];
        expect("empty range", buildPlanSegments(VariationSettings{}, grid, 5.0, 5.0, seg, 2) == 0);
        expect("no output", buildPlanSegments(VariationSettings{}, grid, 0.0, 5.0, nullptr, 2) == 0);
        const auto n = buildPlanSegments(placed(), grid, 0.0, 40.0, seg, 2);
        expect("capacity limits the count and leaves the rest uncovered", n == 2 && seg[1].endBeat < 40.0);
    }

    // Placement: effects only inside the window; segments are contiguous and settings gate correctly.
    {
        const auto scene = placed();
        std::vector<PlanSegment> seg(64);
        seg.resize(buildPlanSegments(scene, grid, 0.0, 32.0, seg.data(), seg.size()));
        bool contiguous = !seg.empty() && seg.front().startBeat == 0.0 && std::abs(seg.back().endBeat - 32.0) < 1e-9;
        for (std::size_t i = 1; i < seg.size(); ++i) contiguous = contiguous && std::abs(seg[i].startBeat - seg[i - 1].endBeat) < 1e-9;
        expect("segments are contiguous over the range", contiguous);
        // Beats 0..8 are outside the window (first half of the phrase): all effects gated off; 8..16 inside.
        const PlanSegment* outside = nullptr; const PlanSegment* inside = nullptr;
        for (const auto& s : seg) {
            if (s.startBeat <= 4.0 && s.endBeat > 4.0) outside = &s;
            if (s.startBeat <= 12.0 && s.endBeat > 12.0) inside = &s;
        }
        expect("outside the window effects are off", outside && !outside->settings.operations[0].enabled && !outside->settings.operations[1].enabled);
        expect("inside the window effects are on", inside && inside->settings.operations[0].enabled && inside->settings.operations[1].enabled);
    }

    // Consistency with the engine driven like the processor: planned source beat matches the trace-resolved one.
    for (int variant = 0; variant < 2; ++variant) {
        VariationSettings scene = placed();
        if (variant == 1) { // event lane: a half-depth repeat event with its own slice and a cycle restart
            scene.control.placement.enabled = false;
            scene.control.lane.explicitEvents = true;
            const auto pt = grid.phraseTicks();
            (void)ctl::addEvent(scene.control.lane, ctl::LaneEvent{0, true, 2 * 960, 6 * 960, 1.0f, 0.5f, 50, true}, pt);
            (void)ctl::addEvent(scene.control.lane, ctl::LaneEvent{1, true, 10 * 960, 4 * 960, 1.0f, 0.0f, 50, false}, pt);
        }
        const auto f = processorLikeRun(scene);
        const FrameWindow w{f.data(), f.size()};
        std::vector<PlanSegment> seg(256);
        seg.resize(buildPlanSegments(scene, grid, 0.0, 40.0, seg.data(), seg.size()));
        PlanContext ctx;
        ctx.segments = seg.data(); ctx.segmentCount = seg.size(); ctx.intensity = 1.0f; ctx.samplesPerBeat = spb;
        ctx.historySamples = static_cast<std::size_t>(std::ceil(RhythmEngine::maxHistorySeconds * sampleRate));
        std::size_t compared = 0, agree = 0, movedAgree = 0, moved = 0;
        for (double beat = 12.0; beat < 39.5; beat += 0.013) {
            const auto sample = sampleAtBeat(w, beat);
            if (!sample) continue;
            const auto heard = resolveThread(w, *sample);
            const auto heardBeat = heard.noData ? std::nullopt : beatAtSample(w, heard.sourceSample);
            const auto planned = planThread(ctx, beat);
            if (!heardBeat || !planned.valid) continue;
            ++compared;
            const bool same = std::abs(*heardBeat - planned.sourceBeat) < 0.01;
            if (same) ++agree;
            if (planned.sourceBeat < beat - 0.02) { ++moved; if (same) ++movedAgree; }
        }
        std::cout << (variant == 0 ? "placement window" : "event lane") << ": compared " << compared << ", agreement "
                  << (compared ? static_cast<double>(agree) / static_cast<double>(compared) : 0.0) << ", moved threads " << moved
                  << " agreement " << (moved ? static_cast<double>(movedAgree) / static_cast<double>(moved) : 0.0) << "\n";
        expect("enough samples", compared > 1000);
        expect("some threads are moved by effects", moved > 100);
        expect("planner matches the engine under placement gating", static_cast<double>(agree) / static_cast<double>(compared) > 0.93);
        expect("moved threads agree too", static_cast<double>(movedAgree) / static_cast<double>(moved) > 0.9);
    }

    if (failures == 0) std::cout << "PlanBuilderTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
