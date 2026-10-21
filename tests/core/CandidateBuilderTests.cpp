#include "core/control/SceneControl.h"
#include "core/dsp/DemoSource.h"
#include "core/scene/SceneBank.h"
#include "core/variation/CandidateBuilder.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::variation;

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
} // namespace

int main()
{
    VariationSettings source;
    for (auto& op : source.operations)
        op = {true, 0.5f, 0.25f};
    GeneratorSettings g;
    g.seed = 42;
    g.style = Style::Experimental;

    // Determinism and regenerate.
    const auto a = buildCandidate(source, g);
    const auto b = buildCandidate(source, g);
    expect("same inputs same candidate", scene::sameSettings(a.settings, b.settings));
    bool anyDifferent = false;
    for (std::uint32_t c = 1; c < 16 && !anyDifferent; ++c)
    {
        g.candidate = c;
        anyDifferent = !scene::sameSettings(buildCandidate(source, g).settings, a.settings);
    }
    g.candidate = 0;
    expect("candidates vary", anyDifferent);

    // Source is not mutated; distance 0 returns the source exactly.
    const auto before = source;
    g.distance = 0.0f;
    const auto zero = buildCandidate(source, g);
    expect("distance 0 = source", scene::sameSettings(zero.settings, source) && zero.changedOperations == 0);
    expect("source untouched", scene::sameSettings(source, before));

    // Distance 1 matches the planner; distance monotonically bounds the depth change.
    g.distance = 1.0f;
    const auto full = buildCandidate(source, g);
    g.distance = 0.25f;
    const auto quarter = buildCandidate(source, g);
    for (std::size_t i = 0; i < operationCount; ++i)
    {
        const float dFull = std::abs(full.settings.operations[i].depth - source.operations[i].depth);
        const float dQuarter = std::abs(quarter.settings.operations[i].depth - source.operations[i].depth);
        expect("smaller distance moves depth less", dQuarter <= dFull + 1e-6f);
    }

    // Locks hold regardless of the candidate; other fields still change.
    g.distance = 1.0f;
    VariationSettings locked = source;
    for (auto& l : locked.control.permissions.operationLocks)
        l = control::lock::all;
    const auto kept = buildCandidate(locked, g);
    expect("fully locked scene unchanged", scene::sameSettings(kept.settings, locked));

    // Protected operations are preserved.
    g.protectedOperations = (1u << operationCount) - 1u;
    expect("protected unchanged", buildCandidate(source, g).changedOperations == 0);
    g.protectedOperations = 0;

    // Event-pattern generation is an explicit generator action and uses the same
    // seed/candidate inputs as the operation candidate.
    g.generateEventPatterns = true;
    g.phraseTicks = 4 * 4 * control::ticksPerBeat;
    g.distance = 1.0f;
    const auto patternA = buildCandidate(source, g);
    const auto patternB = buildCandidate(source, g);
    expect("event generation enables explicit events", patternA.settings.control.lane.explicitEvents);
    expect("event generation produces events", patternA.settings.control.lane.count > 0);
    expect("event-only candidate is reported as changed", patternA.changedEvents);
    expect("event generation is deterministic", control::sameControl(patternA.settings.control, patternB.settings.control));
    bool patternChanged = false;
    for (std::uint32_t c = 1; c < 32 && !patternChanged; ++c)
    {
        g.candidate = c;
        patternChanged = !control::sameControl(patternA.settings.control, buildCandidate(source, g).settings.control);
    }
    expect("event candidates vary", patternChanged);
    g.candidate = 0;

    g.distance = 0.0f;
    const auto noPatternAtZero = buildCandidate(source, g);
    expect("distance zero preserves lane", control::sameControl(source.control, noPatternAtZero.settings.control));
    g.distance = 1.0f;

    // Locked events retain their exact data while unlocked lanes can gain a generated pattern.
    VariationSettings eventLocked = source;
    eventLocked.control.lane.explicitEvents = true;
    eventLocked.control.lane.loopBeats[0] = 3;
    control::LaneEvent fixed;
    fixed.operation = 0;
    fixed.onsetTicks = 123;
    fixed.durationTicks = 456;
    fixed.depth = 0.37f;
    fixed.locked = true;
    eventLocked.control.lane.events[0] = fixed;
    eventLocked.control.lane.count = 1;
    const auto protectedLane = buildCandidate(eventLocked, g).settings.control.lane;
    bool foundLockedEvent = false;
    for (std::size_t i = 0; i < protectedLane.count; ++i)
    {
        const auto& event = protectedLane.events[i];
        if (event.operation == fixed.operation && event.onsetTicks == fixed.onsetTicks
            && event.durationTicks == fixed.durationTicks && event.depth == fixed.depth && event.locked)
            foundLockedEvent = true;
    }
    expect("locked event retained exactly", foundLockedEvent);
    expect("lane loop lengths retained", protectedLane.loopBeats[0] == 3);

    // The operation-level Events lock preserves that lane's events; placement lock freezes the lane.
    VariationSettings operationLocked = eventLocked;
    operationLocked.control.permissions.operationLocks[0] |= control::lock::events;
    const auto operationLockedLane = buildCandidate(operationLocked, g).settings.control.lane;
    std::size_t operationLockedEventCount = 0;
    bool retainedOperationLockedEvent = false;
    for (std::size_t i = 0; i < operationLockedLane.count; ++i)
        if (operationLockedLane.events[i].operation == 0)
        {
            ++operationLockedEventCount;
            retainedOperationLockedEvent = operationLockedLane.events[i].onsetTicks == fixed.onsetTicks;
        }
    expect("events-locked lane retained", operationLockedEventCount == 1 && retainedOperationLockedEvent);
    g.protectedOperations = 1u;
    const auto protectedOperationLane = buildCandidate(eventLocked, g).settings.control.lane;
    bool preservedProtectedOperation = false;
    for (std::size_t i = 0; i < protectedOperationLane.count; ++i)
        if (protectedOperationLane.events[i].operation == 0
            && protectedOperationLane.events[i].onsetTicks == fixed.onsetTicks)
            preservedProtectedOperation = true;
    expect("protected operation events retained", preservedProtectedOperation);
    g.protectedOperations = 0;
    VariationSettings placementLocked = eventLocked;
    placementLocked.control.permissions.placementLocked = true;
    expect("placement lock freezes event generation", control::sameControl(placementLocked.control,
                                                                            buildCandidate(placementLocked, g).settings.control));

    // Generated events never overlap protected regions and remain within fixed lane capacity.
    VariationSettings protectedRegion = source;
    protectedRegion.control.placement.regionCount = 1;
    protectedRegion.control.placement.regions[0] = {0, control::ticksPerBeat};
    const auto protectedPattern = buildCandidate(protectedRegion, g).settings.control.lane;
    bool outsideProtected = true;
    for (std::size_t i = 0; i < protectedPattern.count; ++i)
    {
        const auto& event = protectedPattern.events[i];
        outsideProtected = outsideProtected && !(event.onsetTicks < control::ticksPerBeat);
    }
    expect("generated events avoid protected region", outsideProtected);
    expect("generated events fit capacity", protectedPattern.count <= control::maxLaneEvents);
    g.generateEventPatterns = false;

    // Output always valid.
    for (std::uint32_t c = 0; c < 64; ++c)
    {
        g.candidate = c;
        auto cand = buildCandidate(source, g);
        VariationSettings copy = cand.settings;
        expect("candidate already sanitized", !scene::sanitize(copy));
    }

    // Demo source: finite, bounded, deterministic, seek-invariant.
    {
        std::vector<float> l(512), r(512), l2(512), r2(512);
        float* ch[2] = {l.data(), r.data()};
        float* ch2[2] = {l2.data(), r2.data()};
        DemoSource::render(ch, 2, 512, 3.99, 24000.0, 48000.0);
        DemoSource::render(ch2, 2, 256, 3.99, 24000.0, 48000.0);
        DemoSource::render(ch2 + 0, 2, 0, 0.0, 24000.0, 48000.0);
        bool finite = true, nonzero = false, identical = true;
        for (std::size_t i = 0; i < 512; ++i)
        {
            finite = finite && std::isfinite(l[i]) && std::abs(l[i]) <= 1.0f;
            nonzero = nonzero || std::abs(l[i]) > 1e-3f;
        }
        for (std::size_t i = 0; i < 256; ++i)
            identical = identical && l[i] == l2[i] && l[i] == r[i];
        expect("demo finite and bounded", finite);
        expect("demo audible", nonzero);
        expect("demo deterministic across block splits", identical);
        float junk[4] = {1, 1, 1, 1};
        float* jc[1] = {junk};
        DemoSource::render(jc, 1, 4, std::nan(""), 24000.0, 48000.0);
        DemoSource::render(jc, 1, 4, 0.0, 0.0, 48000.0);
        expect("invalid timing leaves buffer", junk[0] == 1.0f);
    }


    // Demo output is a pure function of the absolute position, including negative pre-roll.
    {
        const double spb = 24000.0;
        std::vector<float> whole(2048), split(2048);
        float* w[1] = {whole.data()};
        DemoSource::render(w, 1, 2048, -2.3, spb, 48000.0);
        for (int part = 0; part < 2048; part += 256)
        {
            float* s2[1] = {split.data() + part};
            DemoSource::render(s2, 1, 256, -2.3 + static_cast<double>(part) / spb, spb, 48000.0);
        }
        double worst = 0.0;
        for (std::size_t i = 0; i < 2048; ++i)
            worst = std::max(worst, static_cast<double>(std::abs(whole[i] - split[i])));
        expect("demo identical when rendered in different block splits (incl. negative positions)", worst < 1e-4);
    }


    // Audit regression: (seed, candidate 1) must not equal another seed's candidate 0.
    {
        const std::uint64_t K = 0xd6e8feb86659fd93ULL;
        int identical = 0;
        for (std::uint64_t seed = 1; seed <= 32; ++seed)
        {
            GeneratorSettings a, b;
            a.seed = seed;
            a.candidate = 1;
            a.style = Style::Experimental;
            b = a;
            b.seed = seed ^ K;
            b.candidate = 0;
            if (scene::sameSettings(buildCandidate(source, a).settings, buildCandidate(source, b).settings))
                ++identical;
        }
        expect("seed and candidate index no longer alias", identical < 8);
    }

    if (failures == 0)
        std::cout << "CandidateBuilderTests passed\n";
    return failures == 0 ? 0 : 1;
}
