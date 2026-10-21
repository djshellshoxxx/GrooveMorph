#include "core/control/Chance.h"
#include "core/control/EuclidPattern.h"
#include "core/control/PlacementEvaluator.h"
#include "core/control/SceneControl.h"
#include "core/control/VariationBlend.h"
#include "core/scene/SceneSerializer.h"
#include "core/viz/PlanBuilder.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::control;

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

const GridContext grid{4, 4};                       // 16 beats per phrase
const std::int64_t phraseTicks = grid.phraseTicks(); // 15360

std::string pattern(int steps, int pulses, int rotation = 0)
{
    EuclidParams p;
    p.steps = static_cast<std::uint8_t>(steps);
    p.pulses = static_cast<std::uint8_t>(pulses);
    p.rotation = static_cast<std::int8_t>(rotation);
    const auto s = euclidSteps(p);
    std::string out;
    for (int i = 0; i < steps; ++i)
        out += s[static_cast<std::size_t>(i)] ? 'x' : '.';
    return out;
}

std::vector<LaneEvent> eventsOf(const VariationSettings& s, std::uint8_t op)
{
    std::vector<LaneEvent> out;
    for (std::size_t i = 0; i < s.control.lane.count; ++i)
        if (s.control.lane.events[i].operation == op)
            out.push_back(s.control.lane.events[i]);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.onsetTicks < b.onsetTicks; });
    return out;
}

LaneEvent makeEvent(std::uint8_t op, int onset, int duration, std::uint8_t chance = 100)
{
    LaneEvent e;
    e.operation = op;
    e.onsetTicks = onset;
    e.durationTicks = duration;
    e.chance = chance;
    return e;
}

SceneControl singleEventScene(std::uint8_t chancePercent, std::uint32_t seed, std::int32_t onset = 0,
                              std::int32_t duration = ticksPerBeat)
{
    SceneControl c;
    c.lane.explicitEvents = true;
    c.lane.chanceSeed = seed;
    c.lane.events[0] = makeEvent(0, onset, duration, chancePercent);
    c.lane.count = 1;
    return c;
}
} // namespace

int main()
{
    // N03-T01 known rhythms.
    expect("E(3,8)", pattern(8, 3) == "x..x..x.");
    expect("E(5,8)", pattern(8, 5) == "x.xx.xx.");
    expect("E(7,16)", pattern(16, 7) == "x..x.x.x..x.x.x.");
    expect("E(2,5)", pattern(5, 2) == "x.x..");
    // The standard Bjorklund output for E(3,4) is "xxx.", a rotation of Toussaint's "x.xx" (same necklace).
    {
        bool necklace = false;
        const std::string a = pattern(4, 3), target = "x.xx";
        for (std::size_t r = 0; r < 4; ++r)
            necklace = necklace || (target.substr(r) + target.substr(0, r)) == a;
        expect("E(3,4) is a rotation of x.xx", necklace && a == "xxx.");
    }
    expect("E(4,9)", pattern(9, 4) == "x.x.x.x..");
    expect("E(5,12)", pattern(12, 5) == "x..x.x..x.x.");
    expect("E(0,6)", pattern(6, 0) == "......");
    expect("E(6,6)", pattern(6, 6) == "xxxxxx");
    expect("E(1,7)", pattern(7, 1) == "x......");
    expect("E(1,1)", pattern(1, 1) == "x");

    // N03-T02 counts and maximal evenness for every steps/pulses pair.
    {
        bool counts = true, even = true, startsWithHit = true;
        for (int steps = 1; steps <= euclidMaxSteps; ++steps)
            for (int pulses = 0; pulses <= steps; ++pulses)
            {
                const auto s = bjorklund(steps, pulses);
                int n = 0;
                std::vector<int> hits;
                for (int i = 0; i < steps; ++i)
                    if (s[static_cast<std::size_t>(i)])
                    {
                        ++n;
                        hits.push_back(i);
                    }
                counts = counts && n == pulses;
                for (int i = steps; i < euclidMaxSteps; ++i)
                    counts = counts && !s[static_cast<std::size_t>(i)];
                if (pulses > 0)
                    startsWithHit = startsWithHit && s[0];
                if (pulses > 1)
                {
                    int lo = steps, hi = 0;
                    for (std::size_t k = 0; k < hits.size(); ++k)
                    {
                        const int next = hits[(k + 1) % hits.size()];
                        const int gap = (next - hits[k] + steps) % steps == 0 ? steps : (next - hits[k] + steps) % steps;
                        lo = std::min(lo, gap);
                        hi = std::max(hi, gap);
                    }
                    even = even && hi - lo <= 1;
                }
            }
        expect("pulse counts exact for all 1..32", counts);
        expect("gaps differ by at most one", even);
        expect("pattern starts with a hit", startsWithHit);
        expect("bjorklund clamps bad input", bjorklund(100, 100)[31] && bjorklund(-3, -3)[0] == false);
    }

    // N03-T03 rotation.
    expect("rotate +1 later", pattern(8, 3, 1) == ".x..x..x");
    expect("rotate +3 later", pattern(8, 3, 3) == ".x.x..x.");
    expect("rotate -1 earlier", pattern(8, 3, -1) == "..x..x.x" && pattern(8, 3, -1) == pattern(8, 3, 7));
    expect("rotation periodic", pattern(8, 5, 8) == pattern(8, 5, 0) && pattern(8, 5, 11) == pattern(8, 5, 3));

    // N03-T04 placement on the grid.
    VariationSettings base;
    base[OperationType::Gate].enabled = true;
    EuclidParams e38;
    e38.steps = 8;
    e38.pulses = 3;
    e38.subdivision = 2; // 1/8 notes: 480 ticks per step
    e38.operation = 2;
    {
        auto s = base;
        const auto report = applyEuclid(s, e38, phraseTicks);
        expect("apply ok", report.status == EuclidStatus::Ok && report.added == 12);
        expect("explicit lane", s.control.lane.explicitEvents);
        const auto ev = eventsOf(s, 2);
        expect("12 events", ev.size() == 12);
        bool exact = ev.size() == 12;
        for (std::size_t t = 0; t < 4 && exact; ++t)
        {
            exact = exact && ev[t * 3 + 0].onsetTicks == static_cast<int>(t * 3840 + 0)
                    && ev[t * 3 + 1].onsetTicks == static_cast<int>(t * 3840 + 1440)
                    && ev[t * 3 + 2].onsetTicks == static_cast<int>(t * 3840 + 2880);
        }
        expect("E(3,8)/8th onsets exact", exact);
        expect("gate 50% of a step", !ev.empty() && ev[0].durationTicks == 240);
        expect("default depth and chance", !ev.empty() && ev[0].depth == 1.0f && ev[0].chance == 100);

        // N03-T05 deterministic, idempotent, repeat off.
        auto again = s;
        applyEuclid(again, e38, phraseTicks);
        expect("apply idempotent", sameControl(again.control, s.control));
        auto fresh = base;
        applyEuclid(fresh, e38, phraseTicks);
        expect("apply deterministic", sameControl(fresh.control, s.control));
        auto once = base;
        auto single = e38;
        single.repeat = false;
        const auto r1 = applyEuclid(once, single, phraseTicks);
        expect("repeat off = one tile", r1.added == 3 && eventsOf(once, 2).size() == 3);
        auto gate100 = e38;
        gate100.gatePercent = 100;
        auto g = base;
        applyEuclid(g, gate100, phraseTicks);
        expect("gate 100 = full step", eventsOf(g, 2)[0].durationTicks == 480);
        auto clamp = e38;
        clamp.steps = 200;
        clamp.pulses = 250;
        clamp.subdivision = 5;
        clamp.gatePercent = 0;
        clamp.operation = 99;
        clamp.density = 250;
        expect("sanitize clamps", sanitize(clamp) && clamp.steps == 32 && clamp.pulses == 32 && clamp.subdivision == 4
                                      && clamp.gatePercent == 10 && clamp.operation == laneCount - 1 && clamp.density == 100);
        auto bad = e38;
        bad.operation = 9;
        auto invalid = base;
        expect("invalid operation rejected", applyEuclid(invalid, bad, phraseTicks).status == EuclidStatus::Invalid
                                                 && isDefault(invalid.control));
    }

    // N03-T06 capacity.
    {
        EuclidParams dense;
        dense.steps = 32;
        dense.pulses = 32;
        dense.subdivision = 16;
        dense.operation = 0;
        dense.gatePercent = 100;
        const std::int64_t longPhrase = 8 * 4 * ticksPerBeat; // 512 pulses of 60 ticks
        VariationSettings s; // nothing enabled: no legacy conversion events
        const auto r = applyEuclid(s, dense, longPhrase);
        expect("dense fills to capacity", s.control.lane.count == maxLaneEvents && r.added == maxLaneEvents);
        expect("dense reports skipped", r.skippedCapacity > 0);

        // Lane with two free slots keeps everything else and takes exactly two.
        auto partly = base;
        partly.control.lane.explicitEvents = true;
        for (std::size_t i = 0; partly.control.lane.count + 2 < maxLaneEvents; ++i)
        {
            const auto op = static_cast<std::uint8_t>(1 + i % 4);
            expect("fill ok", addEvent(partly.control.lane, makeEvent(op, static_cast<int>(i / 4) * 480, 240), longPhrase)
                                  == EditStatus::Ok);
        }
        const auto others = partly.control.lane.count;
        const auto r2 = applyEuclid(partly, dense, longPhrase);
        expect("two free slots used", r2.added == 2 && partly.control.lane.count == maxLaneEvents
                                          && partly.control.lane.count == others + 2);
    }

    // N03-T07 locks.
    {
        auto locked = base;
        locked.control.permissions.operationLocks[2] = lock::events;
        const auto r = applyEuclid(locked, e38, phraseTicks);
        expect("events lock refuses", r.status == EuclidStatus::OperationLocked && !locked.control.lane.explicitEvents
                                           && locked.control.lane.count == 0);
        EuclidReport rep;
        const auto cand = buildEuclidCandidate(locked, e38, phraseTicks, &rep);
        expect("events lock candidate = source", rep.status == EuclidStatus::OperationLocked && sameControl(cand.control, locked.control));
        expect("clear refused by events lock", clearEuclidOperation(locked, 2).status == EuclidStatus::OperationLocked);

        auto withLockedEvent = base;
        withLockedEvent.control.lane.explicitEvents = true;
        LaneEvent le = makeEvent(2, 0, 240);
        le.locked = true;
        le.chance = 77;
        expect("locked event added", addEvent(withLockedEvent.control.lane, le, phraseTicks) == EditStatus::Ok);
        const auto freeEvent = makeEvent(2, 1000, 100); // unlocked, replaced by Apply
        expect("free event added", addEvent(withLockedEvent.control.lane, freeEvent, phraseTicks) == EditStatus::Ok);
        const auto rl = applyEuclid(withLockedEvent, e38, phraseTicks);
        const auto ev = eventsOf(withLockedEvent, 2);
        expect("locked event kept", ev[0].onsetTicks == 0 && ev[0].locked && ev[0].chance == 77 && rl.keptLocked == 1);
        expect("unlocked replaced", rl.removed == 1 && ev.size() == 1 + 11);
        expect("locked blocks overlapping pulse", rl.skippedOverlap == 1 && rl.added == 11);
        const auto cleared = buildClearCandidate(withLockedEvent, 2, phraseTicks);
        const auto left = eventsOf(cleared, 2);
        expect("clear keeps locked only", left.size() == 1 && left[0].locked);
    }

    // N03-T08 protected regions and placement.
    {
        auto s = base;
        s.control.placement.regionCount = 1;
        s.control.placement.regions[0] = ProtectedRegion{0, 960};
        s.control.permissions.placementLocked = true;
        const auto placement = s.control.placement;
        const auto r = applyEuclid(s, e38, phraseTicks);
        expect("region pulse skipped", r.skippedRegion == 1 && r.added == 11);
        bool none = true;
        for (const auto& e : eventsOf(s, 2))
            none = none && !(e.onsetTicks < 960);
        expect("nothing inside region", none);
        expect("placement untouched", s.control.placement.regionCount == placement.regionCount
                                          && s.control.placement.enabled == placement.enabled
                                          && s.control.placement.subdivision == placement.subdivision);
        EuclidReport rep;
        const auto cand = buildEuclidCandidate(base, e38, phraseTicks, &rep);
        expect("candidate leaves source alone", isDefault(base.control) && rep.added == 12 && !isDefault(cand.control));
    }

    // N03-T09 legacy uniform scene keeps other operations sounding.
    {
        auto s = base;
        s[OperationType::Repeat].enabled = true;
        s[OperationType::Gate].enabled = true;
        EuclidParams p = e38;
        p.operation = 1;
        const auto r = applyEuclid(s, p, phraseTicks);
        expect("legacy converted", r.convertedLegacy && s.control.lane.explicitEvents);
        const auto rep = eventsOf(s, 0);
        const auto gate = eventsOf(s, 2);
        expect("other ops are one full-phrase event", rep.size() == 1 && rep[0].durationTicks == phraseTicks
                                                         && gate.size() == 1 && gate[0].durationTicks == phraseTicks);
        expect("target has pattern only", eventsOf(s, 1).size() == 12);
    }

    // N03-T10 candidate = apply + enforce, idempotent through enforcement.
    {
        auto source = base;
        source.control.lane.explicitEvents = true;
        LaneEvent le = makeEvent(2, 0, 240);
        le.locked = true;
        (void)addEvent(source.control.lane, le, phraseTicks);
        const auto cand = buildEuclidCandidate(source, e38, phraseTicks);
        auto manual = source;
        applyEuclid(manual, e38, phraseTicks);
        enforcePermissions(source, manual, true, phraseTicks);
        expect("candidate equals manual path", sameControl(cand.control, manual.control));
        auto again = cand;
        expect("enforcement restores nothing more", enforcePermissions(source, again, true, phraseTicks) == 0);
    }

    // N03-T11 accent and density.
    {
        EuclidParams p;
        p.steps = 8;
        p.pulses = 4;
        p.subdivision = 2;
        p.operation = 0;
        p.accentEvery = 2;
        p.accentDepth = 1.0f;
        p.softDepth = 0.5f;
        p.density = 40;
        p.repeat = false;
        auto s = base;
        applyEuclid(s, p, phraseTicks, 2);
        const auto ev = eventsOf(s, 0);
        expect("4 events", ev.size() == 4);
        expect("accents on pulses 0 and 2", ev.size() == 4 && ev[0].depth == 1.0f && ev[2].depth == 1.0f
                                                && ev[1].depth == 0.5f && ev[3].depth == 0.5f);
        expect("accents always fire, soft use density", ev.size() == 4 && ev[0].chance == 100 && ev[2].chance == 100
                                                           && ev[1].chance == 40 && ev[3].chance == 40);
        expect("scene seed assigned", s.control.lane.chanceSeed != 0);
        auto other = base;
        applyEuclid(other, p, phraseTicks, 3);
        expect("seed depends on scene", other.control.lane.chanceSeed != s.control.lane.chanceSeed);
        auto plain = base;
        auto q = p;
        q.accentEvery = 0;
        q.density = 100;
        applyEuclid(plain, q, phraseTicks, 1);
        expect("no chance, no seed", plain.control.lane.chanceSeed == 0);
    }

    // N03-T12 hash behavior.
    {
        const auto h = chance::hash(1, 0, 0, 0);
        expect("hash pure", h == chance::hash(1, 0, 0, 0));
        expect("hash seed", h != chance::hash(2, 0, 0, 0));
        expect("hash op", h != chance::hash(1, 1, 0, 0));
        expect("hash event", h != chance::hash(1, 0, 480, 0));
        expect("hash cycle", h != chance::hash(1, 0, 0, 1));
        expect("hash negative cycle", chance::hash(1, 0, 0, -1) != chance::hash(1, 0, 0, 1));
        expect("100% always", chance::passes(0, chance::probability(100, 100)) && chance::passes(~0ull, chance::fullScale));
        expect("0% never", !chance::passes(0, chance::probability(0, 100)) && !chance::passes(~0ull, chance::probability(100, 0)));
        SceneControl c = singleEventScene(100, 5);
        bool all = true;
        for (int cyc = 0; cyc < 200; ++cyc)
            all = all && eventFires(c.lane, c.lane.events[0], cyc);
        expect("chance 100 always fires", all);
        c.lane.events[0].chance = 0;
        bool none = true;
        for (int cyc = 0; cyc < 200; ++cyc)
            none = none && !eventFires(c.lane, c.lane.events[0], cyc);
        expect("chance 0 never fires", none);
    }

    // N03-T13 block and segment invariance: walking the timeline in any block size sees the same state
    // as evaluating each position directly, and the reported segment end is exact.
    {
        SceneControl c;
        c.lane.explicitEvents = true;
        c.lane.chanceSeed = 0xC0FFEEu;
        c.lane.chanceByOp[1] = 70;
        c.lane.events[0] = makeEvent(0, 0, 960, 50);
        c.lane.events[1] = makeEvent(0, 4800, 1920, 30);
        c.lane.events[2] = makeEvent(1, 2000, 4000, 80);
        c.lane.events[3] = makeEvent(2, 14400, 3840, 60); // wraps the phrase end
        c.lane.count = 4;
        const double span = 16.0 * 64.0; // 64 phrases
        const double stepBeats = 1.0 / 64.0;
        std::vector<std::uint8_t> reference;
        for (double b = 0.0; b < span; b += stepBeats)
        {
            const auto t = evaluate(c, grid, b, 0x1fu);
            reference.push_back(static_cast<std::uint8_t>((t.active[0] ? 1 : 0) | (t.active[1] ? 2 : 0) | (t.active[2] ? 4 : 0)));
        }
        bool equalAll = true;
        for (const double block : {0.07, 0.3, 1.0, 2.5, 16.0})
        {
            std::vector<std::uint8_t> walked;
            double pos = 0.0;
            std::size_t sample = 0;
            while (pos < span)
            {
                const double blockEnd = std::min(span, pos + block);
                double p = pos;
                while (p < blockEnd - 1e-12)
                {
                    const auto t = evaluate(c, grid, p, 0x1fu);
                    const double end = std::min(blockEnd, p + t.ticksUntilChange / ticksPerBeat);
                    const auto state = static_cast<std::uint8_t>((t.active[0] ? 1 : 0) | (t.active[1] ? 2 : 0) | (t.active[2] ? 4 : 0));
                    // Every reference sample inside [p, end) must show this state.
                    while (sample < reference.size() && static_cast<double>(sample) * stepBeats < end - 1e-9)
                    {
                        if (static_cast<double>(sample) * stepBeats >= p - 1e-9)
                            equalAll = equalAll && reference[sample] == state;
                        ++sample;
                    }
                    p = std::max(end, p + 1.0 / ticksPerBeat);
                }
                pos = blockEnd;
            }
            sample = 0;
        }
        expect("segments identical for all block sizes", equalAll);
        std::size_t onSamples = 0;
        for (auto v : reference)
            onSamples += (v & 1) != 0;
        expect("chance produces fired samples", onSamples > 0);
    }

    // N03-T14 statistics (deterministic with a fixed seed).
    {
        const auto rate = [](std::uint8_t eventChance, std::uint8_t laneChance, std::uint32_t seed) {
            SceneControl c = singleEventScene(eventChance, seed);
            c.lane.chanceByOp[0] = laneChance;
            int fired = 0;
            const int cycles = 20000;
            for (int k = 0; k < cycles; ++k)
                fired += evaluate(c, grid, k * 16.0 + 0.5, 0x1fu).active[0] ? 1 : 0;
            return static_cast<double>(fired) / cycles;
        };
        expect("25% within tolerance", std::abs(rate(25, 100, 7) - 0.25) < 0.02);
        expect("50% within tolerance", std::abs(rate(50, 100, 7) - 0.50) < 0.02);
        expect("80% within tolerance", std::abs(rate(80, 100, 11) - 0.80) < 0.02);
        expect("event x lane = 25%", std::abs(rate(50, 50, 3) - 0.25) < 0.02);
        expect("lane alone", std::abs(rate(100, 10, 3) - 0.10) < 0.02);
        expect("1% is rare but present", rate(1, 100, 9) > 0.0 && rate(1, 100, 9) < 0.03);
        expect("seed reproducible", rate(50, 100, 21) == rate(50, 100, 21));
    }

    // N03-T15 seeds.
    {
        const auto sequence = [](std::uint32_t seed) {
            SceneControl c = singleEventScene(50, seed);
            std::vector<bool> out;
            for (int k = 0; k < 64; ++k)
                out.push_back(evaluate(c, grid, k * 16.0 + 0.5, 0x1fu).active[0]);
            return out;
        };
        expect("same seed same sequence", sequence(1) == sequence(1));
        expect("different seed different sequence", sequence(1) != sequence(2));
        EventLane a, b;
        ensureChanceSeed(a, 0);
        ensureChanceSeed(b, 1);
        expect("scene seeds differ and are non-zero", a.chanceSeed != 0 && b.chanceSeed != 0 && a.chanceSeed != b.chanceSeed);
        const auto kept = a.chanceSeed;
        expect("ensure keeps an existing seed", !ensureChanceSeed(a, 5) && a.chanceSeed == kept);
    }

    // N03-T16 cycle index: loop ring and wrap-around.
    {
        SceneControl c = singleEventScene(50, 99);
        c.lane.loopBeats[0] = 3;
        bool matches = true;
        for (int k = 0; k < 400; ++k)
        {
            const bool want = eventFires(c.lane, c.lane.events[0], k);
            matches = matches && evaluate(c, grid, k * 3.0 + 0.5, 0x1fu).active[0] == want;
        }
        expect("loop lane uses loop cycle", matches);

        SceneControl w = singleEventScene(50, 99, 2400, ticksPerBeat); // 2.5 beats, 1 beat long, loop of 3 beats
        w.lane.loopBeats[0] = 3;
        bool wrapOnce = true;
        for (int k = 0; k < 400; ++k)
        {
            const bool head = evaluate(w, grid, k * 3.0 + 2.7, 0x1fu).active[0];
            const bool tail = evaluate(w, grid, (k + 1) * 3.0 + 0.2, 0x1fu).active[0];
            wrapOnce = wrapOnce && head == tail;
        }
        expect("wrapped event decides once per cycle", wrapOnce);

        SceneControl p = singleEventScene(50, 99, 14880, ticksPerBeat); // 15.5 beats, phrase wrap
        bool phraseWrap = true;
        for (int k = 0; k < 400; ++k)
        {
            const bool head = evaluate(p, grid, k * 16.0 + 15.7, 0x1fu).active[0];
            const bool tail = evaluate(p, grid, (k + 1) * 16.0 + 0.2, 0x1fu).active[0];
            phraseWrap = phraseWrap && head == tail;
        }
        expect("phrase wrap decides once per phrase", phraseWrap);
    }

    // N03-T17 suppressed events do not consume activity-limit slots.
    {
        SceneControl c;
        c.lane.explicitEvents = true;
        c.lane.chanceSeed = 4;
        c.lane.events[0] = makeEvent(0, 0, 3840, 0); // never fires
        c.lane.events[0].priority = 90;
        c.lane.events[1] = makeEvent(1, 0, 3840, 100);
        c.lane.events[1].priority = 10;
        c.lane.count = 2;
        c.activity.enabled = true;
        c.activity.maxConcurrent = 1;
        const auto t = evaluate(c, grid, 1.0, 0x3u);
        expect("silent high-priority event frees the slot", !t.active[0] && t.active[1]);
        c.lane.events[0].chance = 100;
        const auto t2 = evaluate(c, grid, 1.0, 0x3u);
        expect("firing high-priority event wins", t2.active[0] && !t2.active[1]);
    }

    // N03-T18 serialization.
    {
        using namespace groovemorph::core::scene;
        SceneBank bank;
        auto s = bank.variation(0).settings;
        s[OperationType::Repeat].enabled = true;
        s.control.lane.explicitEvents = true;
        s.control.lane.events[0] = makeEvent(0, 0, 960, 37);
        s.control.lane.events[1] = makeEvent(0, 1920, 960, 100);
        s.control.lane.count = 2;
        s.control.lane.chanceByOp[0] = 60;
        s.control.lane.chanceByOp[3] = 15;
        s.control.lane.chanceSeed = 123456789u;
        bank.setVariationSettings(0, s);
        const auto text = SceneSerializer::serialize(bank);
        const auto loaded = SceneSerializer::deserialize(text);
        const auto& lc = loaded.bank.variation(0).settings.control.lane;
        expect("chance round trip equal", loaded.bank == bank && loaded.report.status == LoadStatus::Ok);
        expect("chance values", lc.events[0].chance == 37 && lc.events[1].chance == 100 && lc.chanceByOp[0] == 60
                                    && lc.chanceByOp[3] == 15 && lc.chanceByOp[1] == 100 && lc.chanceSeed == 123456789u);
        expect("re-serialize identical", SceneSerializer::serialize(loaded.bank) == text);
        expect("default chance writes no keys", text.find("event.1.chance") == std::string::npos
                                                    && text.find("lane.chance.1") == std::string::npos);

        // Scene without chance: serialization is free of the new keys and loads as 100 %.
        SceneBank plain;
        auto ps = plain.variation(0).settings;
        ps.control.lane.explicitEvents = true;
        ps.control.lane.events[0] = makeEvent(0, 0, 960);
        ps.control.lane.count = 1;
        plain.setVariationSettings(0, ps);
        const auto plainText = SceneSerializer::serialize(plain);
        expect("legacy-equivalent scene has no chance keys", plainText.find("chance") == std::string::npos
                                                                  && plainText.find("lane.seed") == std::string::npos);

        // Hand-written file from before N03.
        const std::string legacy =
            "groovemorph-scenes v1\n"
            "variation.0.lane.explicit=1\n"
            "variation.0.event.0.op=0\n"
            "variation.0.event.0.enabled=1\n"
            "variation.0.event.0.onset=0\n"
            "variation.0.event.0.duration=960\n"
            "variation.0.event.0.depth=1\n";
        const auto old = SceneSerializer::deserialize(legacy);
        const auto& ol = old.bank.variation(0).settings.control.lane;
        expect("legacy file loads", ol.count == 1 && ol.events[0].chance == 100 && ol.chanceSeed == 0
                                        && ol.chanceByOp[0] == 100 && ol.chanceByOp[4] == 100);

        const std::string garbage = legacy
            + "variation.0.event.0.chance=250\n"
              "variation.0.lane.chance.1=-5\n"
              "variation.0.lane.chance.9=5\n"
              "variation.0.lane.chance.2=abc\n"
              "variation.0.lane.seed=99999999999\n";
        const auto bad = SceneSerializer::deserialize(garbage);
        const auto& bl = bad.bank.variation(0).settings.control.lane;
        expect("garbage clamps", bl.events[0].chance == 100 && bl.chanceByOp[1] == 0 && bl.chanceByOp[2] == 100
                                     && bl.chanceSeed == 4294967295u);
    }

    // N03-T19 editing helpers and locks keep chance.
    {
        auto s = base;
        s.control.lane.explicitEvents = true;
        s.control.lane.chanceSeed = 42;
        s.control.lane.chanceByOp[0] = 55;
        (void)addEvent(s.control.lane, makeEvent(0, 0, 240, 35), phraseTicks);
        (void)addEvent(s.control.lane, makeEvent(0, 960, 240, 65), phraseTicks);
        const auto scaledReport = scaleEvents(s.control.lane, 2, 1, phraseTicks, s.control.permissions);
        expect("scale keeps lane chance", scaledReport.scaled == 2 && s.control.lane.chanceByOp[0] == 55
                                              && s.control.lane.chanceSeed == 42);
        expect("scale keeps event chance", eventsOf(s, 0)[0].chance == 35 && eventsOf(s, 0)[1].chance == 65);
        ActivityConfig act;
        act.enabled = true;
        act.minSpacingBeats = 4.0f;
        reduceEvents(s.control.lane, act, s.control.permissions, phraseTicks, true);
        expect("reduce keeps lane chance", s.control.lane.chanceByOp[0] == 55 && s.control.lane.chanceSeed == 42);

        auto source = base;
        source.control.lane.explicitEvents = true;
        LaneEvent locked = makeEvent(0, 0, 480, 100);
        locked.locked = true;
        (void)addEvent(source.control.lane, locked, phraseTicks);
        auto edited = source;
        edited.control.lane.events[0].chance = 20;
        const auto restored = enforcePermissions(source, edited, true, phraseTicks);
        expect("locked event chance cannot change", restored > 0 && edited.control.lane.events[0].chance == 100
                                                        && edited.control.lane.count == 1);

        auto opLocked = source;
        opLocked.control.permissions.operationLocks[0] = lock::events;
        opLocked.control.lane.chanceByOp[0] = 80;
        auto changed = opLocked;
        changed.control.lane.chanceByOp[0] = 10;
        enforcePermissions(opLocked, changed, true, phraseTicks);
        expect("events lock freezes lane chance", changed.control.lane.chanceByOp[0] == 80);

        auto unlockedSource = base;
        unlockedSource.control.lane.explicitEvents = true;
        (void)addEvent(unlockedSource.control.lane, makeEvent(0, 0, 480, 100), phraseTicks);
        auto freeEdit = unlockedSource;
        freeEdit.control.lane.events[0].chance = 20;
        expect("unlocked chance editable", enforcePermissions(unlockedSource, freeEdit, true, phraseTicks) == 0
                                               && freeEdit.control.lane.events[0].chance == 20);
        expect("chance is part of scene equality", !sameControl(unlockedSource.control, freeEdit.control));
        auto sanitized = freeEdit;
        sanitized.control.lane.events[0].chance = 200;
        sanitized.control.lane.chanceByOp[2] = 150;
        expect("sanitize clamps chance", sanitize(sanitized.control) && sanitized.control.lane.events[0].chance == 100
                                             && sanitized.control.lane.chanceByOp[2] == 100);
    }

    // N03-T20 planned path honours chance exactly like evaluate().
    {
        VariationSettings scene;
        scene[OperationType::Repeat].enabled = true;
        scene.control.lane.explicitEvents = true;
        scene.control.lane.chanceSeed = 1234;
        scene.control.lane.events[0] = makeEvent(0, 0, 1920, 50);
        scene.control.lane.events[1] = makeEvent(0, 7680, 3840, 40);
        scene.control.lane.count = 2;
        std::vector<groovemorph::core::viz::PlanSegment> segments(4096);
        const double span = 16.0 * 40.0;
        const auto n = groovemorph::core::viz::buildPlanSegments(scene, grid, 0.0, span, segments.data(), segments.size(), 0.0);
        expect("plan produced segments", n > 40);
        bool same = true;
        std::size_t onSegments = 0, offSegments = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto& seg = segments[i];
            const double mid = 0.5 * (seg.startBeat + seg.endBeat);
            const auto t = evaluate(scene.control, grid, mid, 0x1u, mid);
            same = same && seg.settings.operations[0].enabled == t.active[0];
            onSegments += seg.settings.operations[0].enabled ? 1 : 0;
            offSegments += seg.settings.operations[0].enabled ? 0 : 1;
        }
        expect("plan equals evaluate", same);
        expect("plan mixes fired and suppressed cycles", onSegments > 0 && offSegments > onSegments);
    }

    return failures == 0 ? 0 : 1;
}
