// F02 morph of placement, regions and lane events (docs/specs/W3-MORPH.md).
#include "core/control/PlacementEvaluator.h"
#include "core/control/VariationBlend.h"
#include "core/scene/SceneBank.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::control;

namespace {
int failures = 0;
void expect(const std::string& name, bool ok) {
    if (!ok) { std::cerr << name << ": check failed\n"; ++failures; }
}

constexpr std::int64_t phraseTicks = 4 * 4 * ticksPerBeat;

LaneEvent ev(std::uint8_t op, int onset, int dur, float depth = 1.0f, std::uint8_t prio = 50) {
    LaneEvent e;
    e.operation = op; e.onsetTicks = onset; e.durationTicks = dur; e.depth = depth; e.priority = prio;
    return e;
}

VariationSettings mk(bool gate, bool repeat) {
    VariationSettings v;
    v[OperationType::Gate] = {gate, 1.0f, 0.5f};
    v[OperationType::Repeat] = {repeat, 1.0f, 0.25f};
    return v;
}

void add(VariationSettings& v, const LaneEvent& e) {
    v.control.lane.explicitEvents = true;
    v.control.lane.events[v.control.lane.count++] = e;
}

const LaneEvent* find(const VariationSettings& v, std::uint8_t op, int onset) {
    for (std::size_t i = 0; i < v.control.lane.count; ++i)
        if (v.control.lane.events[i].operation == op && v.control.lane.events[i].onsetTicks == onset)
            return &v.control.lane.events[i];
    return nullptr;
}
} // namespace

int main() {
    const auto gate = static_cast<std::uint8_t>(OperationType::Gate);
    const auto rep = static_cast<std::uint8_t>(OperationType::Repeat);

    // F02-T08a: weight 0 returns a; unsafe weights; scenes without control data equal the old result.
    {
        VariationSettings a = mk(true, false), b = mk(false, true);
        add(a, ev(gate, 0, 960));
        const auto zero = morph(a, b, 0.0f, phraseTicks);
        expect("F02-T08a weight 0 is a", groovemorph::core::scene::sameSettings(zero, a));
        const float nan = std::numeric_limits<float>::quiet_NaN();
        expect("F02-T08a NaN weight is 0", groovemorph::core::scene::sameSettings(morph(a, b, nan, phraseTicks), a));
        expect("F02-T08a negative weight is 0", groovemorph::core::scene::sameSettings(morph(a, b, -3.0f, phraseTicks), a));
        const auto big = morph(a, b, 7.0f, phraseTicks);
        expect("F02-T08a weight > 1 clamps to 1", groovemorph::core::scene::sameSettings(big, morph(a, b, 1.0f, phraseTicks)));
        const VariationSettings plainA = mk(true, false), plainB = mk(false, true);
        const auto m = morph(plainA, plainB, 0.4f, phraseTicks);
        expect("F02-T08a plain scenes keep inert control", isDefault(m.control));
        expect("F02-T08a plain depth fade", std::abs(m.operations[gate].depth - 0.6f) < 1e-6f);
    }

    // F02-T08b: weight 1 equals b's control and event set.
    {
        VariationSettings a = mk(true, true), b = mk(true, true);
        add(a, ev(gate, 0, 960, 0.8f));
        add(a, ev(rep, 1920, 960));
        add(b, ev(gate, 0, 960, 0.4f));
        add(b, ev(gate, 4800, 480));
        b.control.placement.enabled = true;
        b.control.placement.every = 2;
        b.control.placement.regionCount = 2;
        b.control.placement.regions[1] = {1000, 100};
        b.control.activity.enabled = true;
        b.control.activity.maxConcurrent = 1;
        b.control.lane.loopBeats[gate] = 8;
        const auto m = morph(a, b, 1.0f, phraseTicks);
        expect("F02-T08b placement from b", m.control.placement.enabled && m.control.placement.every == 2);
        expect("F02-T08b regions from b", m.control.placement.regionCount == 2 && m.control.placement.regions[1].startTicks == 1000);
        expect("F02-T08b activity from b", m.control.activity.enabled && m.control.activity.maxConcurrent == 1);
        expect("F02-T08b loop from b", m.control.lane.loopBeats[gate] == 8);
        expect("F02-T08b a-only event gone", find(m, rep, 1920) == nullptr);
        const auto* matched = find(m, gate, 0);
        expect("F02-T08b matched event is b's", matched != nullptr && std::abs(matched->depth - 0.4f) < 1e-6f);
        expect("F02-T08b b-only event present", find(m, gate, 4800) != nullptr && std::abs(find(m, gate, 4800)->depth - 1.0f) < 1e-6f);
        expect("F02-T08b count", m.control.lane.count == 2);
    }

    // F02-T08c: monotonic depth of unmatched events, matched between.
    {
        VariationSettings a = mk(true, true), b = mk(true, true);
        add(a, ev(rep, 0, 960, 0.9f));
        add(a, ev(gate, 0, 960, 1.0f));
        add(b, ev(gate, 0, 960, 0.2f));
        add(b, ev(rep, 3840, 960, 0.7f));
        float prevA = 2.0f, prevB = -1.0f;
        bool mono = true, between = true;
        for (int k = 1; k <= 99; ++k) {
            const float w = static_cast<float>(k) / 100.0f;
            const auto m = morph(a, b, w, phraseTicks);
            const auto* onlyA = find(m, rep, 0);
            const auto* onlyB = find(m, rep, 3840);
            const float depthA = onlyA ? onlyA->depth : 0.0f, depthB = onlyB ? onlyB->depth : 0.0f;
            mono = mono && depthA <= prevA + 1e-6f && depthB >= prevB - 1e-6f;
            prevA = depthA; prevB = depthB;
            const auto* g = find(m, gate, 0);
            between = between && g != nullptr && g->depth <= 1.0f + 1e-6f && g->depth >= 0.2f - 1e-6f;
        }
        expect("F02-T08c monotonic unmatched depth", mono);
        expect("F02-T08c matched depth between", between);
    }

    // F02-T08d: threshold at 0.5 for discrete fields; start/span linear.
    {
        VariationSettings a = mk(true, false), b = mk(true, false);
        a.control.placement.enabled = true; a.control.placement.every = 1; a.control.placement.startPhase = 0.0f; a.control.placement.spanPhase = 0.2f;
        b.control.placement.enabled = true; b.control.placement.every = 4; b.control.placement.startPhase = 0.5f; b.control.placement.spanPhase = 0.6f;
        b.control.placement.regionCount = 1; b.control.placement.regions[0] = {960, 960};
        const auto lo = morph(a, b, 0.49f, phraseTicks), hi = morph(a, b, 0.5f, phraseTicks);
        expect("F02-T08d below threshold is a", lo.control.placement.every == 1 && lo.control.placement.regionCount == 0);
        expect("F02-T08d at threshold is b", hi.control.placement.every == 4 && hi.control.placement.regionCount == 1);
        expect("F02-T08d start linear", std::abs(lo.control.placement.startPhase - 0.245f) < 1e-5f);
        expect("F02-T08d span linear", std::abs(hi.control.placement.spanPhase - 0.4f) < 1e-5f);
    }

    // F02-T08e: overlapping same-operation events never both appear.
    {
        VariationSettings a = mk(true, false), b = mk(true, false);
        add(a, ev(gate, 0, 1920));
        add(b, ev(gate, 960, 1920));
        for (int k = 1; k <= 99; ++k) {
            const float w = static_cast<float>(k) / 100.0f;
            const auto m = morph(a, b, w, phraseTicks);
            expect("F02-T08e no overlap at " + std::to_string(k), m.control.lane.count == 1);
            const bool keepsA = find(m, gate, 0) != nullptr;
            if (w < 0.5f) expect("F02-T08e a wins below 0.5", keepsA);
            else expect("F02-T08e b wins at or above 0.5", !keepsA && find(m, gate, 960) != nullptr);
        }
    }

    // F02-T08f: legacy side vs explicit side.
    {
        VariationSettings legacy = mk(true, false), expl = mk(true, false);
        add(expl, ev(gate, 960, 480));
        const auto m = morph(legacy, expl, 0.25f, phraseTicks);
        expect("F02-T08f explicit when either is", m.control.lane.explicitEvents);
        const auto* full = find(m, gate, 0);
        expect("F02-T08f legacy contributes a full-phrase event", full != nullptr && full->durationTicks == phraseTicks);
        const auto both = morph(mk(true, false), mk(false, true), 0.5f, phraseTicks);
        expect("F02-T08f both legacy stays non-explicit", !both.control.lane.explicitEvents && both.control.lane.count == 0);
        // The merged lane really drives the evaluator.
        GridContext grid; grid.beatsPerBar = 4; grid.phraseBars = 4;
        const auto t = evaluate(m.control, grid, 1.0, 0x1fu);
        expect("F02-T08f evaluator sees the merged events", t.gated && t.active[gate]);
    }

    // F02-T08g: capacity and determinism.
    {
        VariationSettings a = mk(true, true), b = mk(true, true);
        for (int i = 0; i < 128; ++i) add(a, ev(static_cast<std::uint8_t>(i % 2), (i / 2) * 200, 100));
        for (int i = 0; i < 128; ++i) add(a, ev(static_cast<std::uint8_t>(2 + i % 2), (i / 2) * 200, 100));
        for (int i = 0; i < 128; ++i) add(b, ev(static_cast<std::uint8_t>(i % 2), 100 + (i / 2) * 200, 50));
        for (int i = 0; i < 128; ++i) add(b, ev(static_cast<std::uint8_t>(2 + i % 2), 100 + (i / 2) * 200, 50));
        const auto m1 = morph(a, b, 0.5f, phraseTicks), m2 = morph(a, b, 0.5f, phraseTicks);
        expect("F02-T08g capacity bound", m1.control.lane.count <= maxLaneEvents);
        expect("F02-T08g deterministic", sameControl(m1.control, m2.control));
        expect("F02-T08g full lane used", m1.control.lane.count > 200);
    }

    // F02-T08h: output is already sane; locks have no effect; inputs untouched.
    {
        VariationSettings a = mk(true, true), b = mk(true, false);
        add(a, ev(gate, 0, 960));
        add(b, ev(gate, 4800, 960, 0.5f));
        a.control.permissions.operationLocks[gate] = lock::all;
        a.control.permissions.placementLocked = true;
        const auto copyA = a, copyB = b;
        auto m = morph(a, b, 0.7f, phraseTicks);
        expect("F02-T08h inputs unchanged", groovemorph::core::scene::sameSettings(a, copyA) && sameControl(a.control, copyA.control)
                                              && groovemorph::core::scene::sameSettings(b, copyB) && sameControl(b.control, copyB.control));
        expect("F02-T08h b-only event present despite locks", find(m, gate, 4800) != nullptr);
        auto before = m.control;
        const bool changed = sanitize(m.control, static_cast<std::int32_t>(phraseTicks));
        expect("F02-T08h result needs no sanitizing", !changed && sameControl(before, m.control));
    }

    if (failures == 0) std::cout << "MorphControlTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
