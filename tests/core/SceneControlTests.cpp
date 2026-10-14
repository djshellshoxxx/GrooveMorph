#include "core/control/AutoReturn.h"
#include "core/control/PlacementEvaluator.h"
#include "core/control/SceneControl.h"
#include "core/control/VariationBlend.h"
#include "core/scene/SceneSerializer.h"

#include <cmath>
#include <iostream>
#include <optional>
#include <string_view>

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
const GridContext grid{4, 4}; // 4/4, 4 bars => 16 beats per phrase
} // namespace

int main()
{
    // Inert control is the identity and never splits segments.
    {
        SceneControl c;
        const auto t = evaluate(c, grid, 3.3);
        expect("inert not gated", !t.gated);
        expect("inert all active", t.active[0] && t.active[4] && t.depthScale[2] == 1.0f);
        expect("inert default", isDefault(c));
    }
    // C01-T01 analogue: LastBar window = final bar of a 4-bar phrase.
    {
        SceneControl c;
        c.placement.enabled = true;
        c.placement.window = WindowMode::LastBar;
        expect("before last bar inactive", !evaluate(c, grid, 11.99).active[0]);
        expect("last bar start active", evaluate(c, grid, 12.0).active[0]);
        expect("last bar end exclusive", !evaluate(c, grid, 16.0).active[0]);
        const auto t = evaluate(c, grid, 11.5);
        expect("boundary distance", std::abs(t.ticksUntilChange - 0.5 * ticksPerBeat) < 1e-6);
        const auto u = evaluate(c, grid, 12.0);
        expect("distance to window end", std::abs(u.ticksUntilChange - 4.0 * ticksPerBeat) < 1e-6);
    }
    // Wrapping custom window: start .875 span .25 = last half bar+first half bar... of 4 bars.
    {
        SceneControl c;
        c.placement.enabled = true;
        c.placement.window = WindowMode::Custom;
        c.placement.startPhase = 0.875f;
        c.placement.spanPhase = 0.25f;
        expect("wrap tail active", evaluate(c, grid, 14.5).active[1]);
        expect("wrap head active", evaluate(c, grid, 1.0).active[1]);
        expect("middle inactive", !evaluate(c, grid, 8.0).active[1]);
        expect("exactly end inactive", !evaluate(c, grid, 2.0).active[1]);
    }
    // C01-T03: Every=4 / offset=3 acts on phrase indices 3,7,11 (incl. negative preroll).
    {
        SceneControl c;
        c.placement.enabled = true;
        c.placement.every = 4;
        c.placement.cadenceOffset = 3;
        for (int idx = -4; idx < 12; ++idx)
        {
            const bool want = ((idx % 4) + 4) % 4 == 3;
            expect("cadence", evaluate(c, grid, idx * 16.0 + 5.0).active[0] == want);
        }
    }
    // Explicit events gate per operation with depth scale; unlisted ops are silent.
    {
        SceneControl c;
        c.lane.explicitEvents = true;
        LaneEvent e;
        e.operation = 2;
        e.onsetTicks = 4 * ticksPerBeat;
        e.durationTicks = 2 * ticksPerBeat;
        e.depth = 0.5f;
        expect("add event", addEvent(c.lane, e, grid.phraseTicks()) == EditStatus::Ok);
        expect("inside", evaluate(c, grid, 5.0).active[2]);
        expect("scale", evaluate(c, grid, 5.0).depthScale[2] == 0.5f);
        expect("outside", !evaluate(c, grid, 7.0).active[2]);
        expect("other op silent", !evaluate(c, grid, 5.0).active[0]);
        const auto t = evaluate(c, grid, 3.75);
        expect("event onset distance", std::abs(t.ticksUntilChange - 0.25 * ticksPerBeat) < 1e-6);
        // overlap, wrapped overlap, capacity, invalid
        LaneEvent o = e;
        o.onsetTicks = 5 * ticksPerBeat;
        expect("overlap rejected", addEvent(c.lane, o, grid.phraseTicks()) == EditStatus::Overlap);
        LaneEvent w;
        w.operation = 2;
        w.onsetTicks = 15 * ticksPerBeat;
        w.durationTicks = 6 * ticksPerBeat; // wraps to beat 5, overlapping the 4..6 event
        expect("wrapped overlap rejected", addEvent(c.lane, w, grid.phraseTicks()) == EditStatus::Overlap);
        w.operation = 3;
        expect("other op wrap ok", addEvent(c.lane, w, grid.phraseTicks()) == EditStatus::Ok);
        LaneEvent bad;
        bad.durationTicks = 0;
        expect("invalid rejected", addEvent(c.lane, bad, grid.phraseTicks()) == EditStatus::Invalid);
        bad = LaneEvent{};
        bad.onsetTicks = static_cast<std::int32_t>(grid.phraseTicks());
        expect("onset at phrase end rejected", addEvent(c.lane, bad, grid.phraseTicks()) == EditStatus::Invalid);
        expect("wrapped event active at start", evaluate(c, grid, 0.5).active[3]);
        expect("remove", removeEvent(c.lane, 0) && c.lane.count == 1);
        EventLane full;
        const auto pt = grid.phraseTicks();
        int added = 0;
        for (int i = 0; i < 40; ++i)
        {
            LaneEvent x;
            x.operation = static_cast<std::uint8_t>(i % 5);
            x.onsetTicks = (i / 5) * 100;
            x.durationTicks = 50;
            if (addEvent(full, x, pt) == EditStatus::Ok)
                ++added;
        }
        expect("capacity bound", added == static_cast<int>(maxLaneEvents) && full.count == maxLaneEvents);
    }
    // Downbeat protection silences each bar start.
    {
        SceneControl c;
        c.placement.downbeatProtectBeats = 1.0f;
        expect("bar 2 downbeat silent", !evaluate(c, grid, 4.0).active[0]);
        expect("beat 2 active", evaluate(c, grid, 5.0).active[0]);
        expect("distance to unprotect", std::abs(evaluate(c, grid, 4.25).ticksUntilChange - 0.75 * ticksPerBeat) < 1e-6);
    }
    // C06 activity limit: highest priority wins, ties by lower index, only enabled ops counted.
    {
        SceneControl c;
        c.activity.enabled = true;
        c.activity.maxConcurrent = 2;
        c.activity.priority = {10, 90, 50, 90, 70};
        const auto t = evaluate(c, grid, 1.0, 0x1fu);
        expect("limit keeps 1 and 3", !t.active[0] && t.active[1] && !t.active[2] && t.active[3] && !t.active[4]);
        const auto u = evaluate(c, grid, 1.0, 0b10101u); // ops 0,2,4 enabled
        expect("enabled-only winners", u.active[4] && u.active[2] && !u.active[0]);
    }
    // Convert legacy -> explicit events.
    {
        EventLane lane;
        expect("convert adds 2", convertLegacyToEvents(lane, 0b00101u, grid.phraseTicks()) == 2 && lane.explicitEvents);
        SceneControl c;
        c.lane = lane;
        expect("converted active everywhere", evaluate(c, grid, 9.9).active[0] && evaluate(c, grid, 9.9).active[2]);
    }
    // Blend / morph / permissions.
    {
        VariationSettings a, b;
        a.operations[0] = {true, 0.2f, 0.25f};
        b.operations[0] = {false, 0.8f, 1.0f};
        const auto zero = blendDistance(a, b, 0.0f);
        const auto one = blendDistance(a, b, 1.0f);
        const auto half = blendDistance(a, b, 0.5f);
        expect("distance 0 identity", zero.operations[0].depth == 0.2f && zero.operations[0].enabled);
        expect("distance 1 candidate", one.operations[0].depth == 0.8f && !one.operations[0].enabled);
        expect("half depth", std::abs(half.operations[0].depth - 0.5f) < 1e-6f);
        expect("half slice geometric", std::abs(half.operations[0].sliceBeats - 0.5f) < 1e-5f);
        VariationSettings x, y;
        x.operations[1] = {true, 1.0f, 0.5f};
        y.operations[2] = {true, 1.0f, 1.0f};
        const auto m = morph(x, y, 0.25f);
        expect("morph fades out", m.operations[1].enabled && std::abs(m.operations[1].depth - 0.75f) < 1e-6f);
        expect("morph fades in", m.operations[2].enabled && std::abs(m.operations[2].depth - 0.25f) < 1e-6f);
        expect("morph weight 0 keeps a", !morph(x, y, 0.0f).operations[2].enabled);
        VariationSettings src;
        src.operations[0] = {true, 0.5f, 0.25f};
        src.control.permissions.operationLocks[0] = lock::depth | lock::enable;
        VariationSettings cand = src;
        cand.operations[0] = {false, 0.9f, 2.0f};
        const auto restored = enforcePermissions(src, cand);
        expect("permissions restore locked", restored == 2 && cand.operations[0].enabled
                                                  && cand.operations[0].depth == 0.5f
                                                  && cand.operations[0].sliceBeats == 2.0f);
    }
    // Auto return.
    {
        AutoReturn r;
        r.arm(5.4, 2, 0); // switch in bar 5; return at bar 7 boundary
        expect("armed", r.armed());
        expect("no request bar 5", !r.poll(5.9));
        expect("request during bar 6", r.poll(6.0) == std::optional<std::size_t>(0));
        expect("only once", !r.poll(6.5));
        AutoReturn s;
        s.arm(10.0, 1, 3);
        expect("seek back disarms", !s.poll(2.0) && !s.armed());
    }
    // Serializer round trip keeps control state; defaults emit no control keys.
    {
        using namespace groovemorph::core::scene;
        SceneBank bank;
        const auto plain = SceneSerializer::serialize(bank);
        expect("default has no control keys", plain.find("placement") == std::string::npos);
        auto v = bank.variation(1);
        v.settings.control.placement.enabled = true;
        v.settings.control.placement.window = WindowMode::Custom;
        v.settings.control.placement.startPhase = 0.25f;
        v.settings.control.placement.spanPhase = 0.5f;
        v.settings.control.placement.every = 4;
        v.settings.control.placement.cadenceOffset = 3;
        v.settings.control.placement.downbeatProtectBeats = 0.5f;
        v.settings.control.permissions.operationLocks[2] = lock::slice;
        v.settings.control.activity.enabled = true;
        v.settings.control.activity.maxConcurrent = 2;
        LaneEvent e;
        e.operation = 1;
        e.onsetTicks = 960;
        e.durationTicks = 480;
        e.depth = 0.75f;
        v.settings.control.lane.explicitEvents = true;
        (void)addEvent(v.settings.control.lane, e, 15360);
        bank.setVariation(1, v);
        const auto text = SceneSerializer::serialize(bank);
        const auto loaded = SceneSerializer::deserialize(text);
        expect("roundtrip ok", loaded.report.status == LoadStatus::Ok);
        expect("roundtrip equal", loaded.bank == bank);
        // legacy file (no control keys) loads inert
        const auto legacy = SceneSerializer::deserialize(plain);
        expect("legacy loads default control", control::isDefault(legacy.bank.variation(1).settings.control));
        // malformed control values are clamped, not trusted
        auto hostile = text + "variation.1.placement.every=999\nvariation.1.event.0.duration=-5\n"
                              "variation.1.event.99.op=1\nvariation.1.activity.max=0\n";
        const auto h = SceneSerializer::deserialize(hostile);
        const auto& hc = h.bank.variation(1).settings.control;
        expect("hostile clamped", hc.placement.every == 64 && hc.lane.events[0].durationTicks >= 1
                                       && hc.activity.maxConcurrent == 1 && hc.lane.count <= maxLaneEvents);
        // duplicate / reset
        SceneBank b2;
        expect("duplicate scene", b2.duplicateVariation(1, 0) && b2.variation(0) == b2.variation(1));
        expect("duplicate self rejected", !b2.duplicateVariation(1, 1));
        b2.resetVariation(0);
        expect("reset scene", b2.variation(0) == SceneBank::defaultVariation(0));
    }

    if (failures == 0)
        std::cout << "SceneControlTests passed\n";
    return failures == 0 ? 0 : 1;
}
