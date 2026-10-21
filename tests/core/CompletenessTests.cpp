// Acceptance tests added by the Beta spec-completeness audit (docs/specs/COMPLETENESS-MATRIX.md).
// Each label starts with the spec acceptance ID it closes so the matrix script finds it by text search.
#include "core/control/GestureLane.h"
#include "core/control/PlacementEvaluator.h"
#include "core/control/SceneControl.h"
#include "core/control/VariationBlend.h"
#include "core/scene/SceneBank.h"
#include "core/scene/SceneSerializer.h"
#include "core/variation/CandidateBuilder.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

using namespace groovemorph::core;
using namespace groovemorph::core::control;
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
bool near(float a, float b, float tol = 1.0e-5f) { return std::abs(a - b) <= tol; }

VariationSettings plainSource()
{
    VariationSettings source;
    for (auto& op : source.operations)
        op = {true, 0.5f, 0.25f};
    return source;
}

// F01-T08: golden candidate. Recorded on GCC 14 / Linux x86-64 at the Beta audit; the CI matrix compiles the
// same file with GCC, Clang and MSVC, so a platform-dependent difference in the generator fails there.
void goldenCandidate()
{
    GeneratorSettings g;
    g.seed = 42;
    g.style = Style::Experimental;
    g.distance = 1.0f;
    g.candidate = 0;
    g.generateEventPatterns = true;
    g.phraseTicks = 16 * ticksPerBeat;
    const auto c = buildCandidate(plainSource(), g);

    struct Op { bool enabled; float depth; float slice; };
    const Op ops[operationCount] = {
        {true, 0.328610f, 0.25f}, {false, 0.323924f, 0.25f}, {false, 0.522015f, 0.25f}, {true, 0.052770f, 0.25f},
        {true, 0.631801f, 0.125f}, {true, 0.986120f, 0.25f}, {true, 0.810372f, 0.25f}};
    bool opsOk = true;
    for (std::size_t i = 0; i < operationCount; ++i)
        opsOk = opsOk && c.settings.operations[i].enabled == ops[i].enabled
                && near(c.settings.operations[i].depth, ops[i].depth, 2.0e-6f)
                && near(c.settings.operations[i].sliceBeats, ops[i].slice, 1.0e-6f);
    expect("F01-T08 golden operation settings", opsOk);

    struct Ev { int op; int onset; int duration; float depth; float slice; bool restart; };
    const Ev events[] = {
        {0, 1200, 480, 0.634516f, 0.125f, false}, {0, 4560, 960, 0.484811f, 1.0f, false},
        {0, 8880, 480, 0.626337f, 1.0f, false},   {3, 8640, 480, 0.476881f, 0.25f, false},
        {3, 4800, 480, 0.552730f, 0.0f, false},   {3, 11040, 480, 0.942360f, 0.5f, false},
        {4, 2400, 960, 0.536573f, 0.125f, true},  {4, 1920, 240, 0.849643f, 0.0f, false},
        {4, 12000, 240, 0.701100f, 0.0f, true},   {5, 3360, 960, 0.646260f, 0.125f, false},
        {5, 240, 240, 0.705938f, 0.5f, true},     {5, 11760, 240, 0.752061f, 0.5f, true},
        {6, 4320, 480, 0.873804f, 0.125f, true},  {6, 14400, 240, 0.903956f, 0.5f, true},
        {6, 8160, 480, 0.911297f, 0.25f, false}};
    const auto& lane = c.settings.control.lane;
    bool laneOk = lane.explicitEvents && lane.count == 15;
    for (std::size_t i = 0; laneOk && i < 15; ++i)
    {
        const auto& e = lane.events[i];
        laneOk = e.operation == events[i].op && e.onsetTicks == events[i].onset
                 && e.durationTicks == events[i].duration && near(e.depth, events[i].depth, 2.0e-6f)
                 && near(e.sliceBeats, events[i].slice, 1.0e-6f) && e.restartCycle == events[i].restart;
    }
    expect("F01-T08 golden event list", laneOk);
}

// F01-T09: generation never exceeds the lane capacity (256 after W3).
void generationCapacity()
{
    VariationSettings source = plainSource();
    auto& lane = source.control.lane;
    lane.explicitEvents = true;
    const std::int64_t phrase = 16 * ticksPerBeat;
    int added = 0;
    for (int k = 0; k < 300; ++k)
    {
        LaneEvent e;
        e.operation = static_cast<std::uint8_t>(k % 7);
        e.onsetTicks = (k / 7) * 100;
        e.durationTicks = 50;
        e.locked = true;
        if (addEvent(lane, e, phrase) == EditStatus::Ok)
            ++added;
    }
    expect("F01-T09 test lane is full", added == static_cast<int>(maxLaneEvents) && lane.count == maxLaneEvents);
    bool within = true;
    bool keptAll = true;
    for (Style style : {Style::Conservative, Style::Creative, Style::Experimental})
        for (std::uint32_t cand = 0; cand < 6; ++cand)
        {
            GeneratorSettings g;
            g.seed = 7;
            g.style = style;
            g.candidate = cand;
            g.generateEventPatterns = true;
            g.phraseTicks = phrase;
            const auto c = buildCandidate(source, g);
            within = within && c.settings.control.lane.count <= maxLaneEvents;
            keptAll = keptAll && c.settings.control.lane.count == maxLaneEvents;
        }
    expect("F01-T09 count never exceeds capacity", within);
    expect("F01-T09 locked events fill the lane and nothing is dropped", keptAll);

    // Unlocked full lane: replaced, still bounded.
    VariationSettings open = plainSource();
    open.control.lane.explicitEvents = true;
    for (int k = 0; k < 300; ++k)
    {
        LaneEvent e;
        e.operation = static_cast<std::uint8_t>(k % 7);
        e.onsetTicks = (k / 7) * 100;
        e.durationTicks = 50;
        (void)addEvent(open.control.lane, e, phrase);
    }
    GeneratorSettings g;
    g.seed = 9;
    g.generateEventPatterns = true;
    g.phraseTicks = phrase;
    expect("F01-T09 unlocked full lane stays bounded", buildCandidate(open, g).settings.control.lane.count <= maxLaneEvents);
}

// F01-T10: only Experimental sets restartCycle.
void styleRestart()
{
    int conservativeRestarts = 0, creativeRestarts = 0, experimentalRestarts = 0;
    for (std::uint64_t seed = 1; seed <= 24; ++seed)
        for (std::uint32_t cand = 0; cand < 4; ++cand)
            for (Style style : {Style::Conservative, Style::Creative, Style::Experimental})
            {
                GeneratorSettings g;
                g.seed = seed;
                g.style = style;
                g.candidate = cand;
                g.generateEventPatterns = true;
                g.phraseTicks = 16 * ticksPerBeat;
                const auto c = buildCandidate(plainSource(), g);
                int restarts = 0;
                for (std::size_t i = 0; i < c.settings.control.lane.count; ++i)
                    restarts += c.settings.control.lane.events[i].restartCycle ? 1 : 0;
                (style == Style::Conservative ? conservativeRestarts
                 : style == Style::Creative   ? creativeRestarts
                                              : experimentalRestarts) += restarts;
            }
    expect("F01-T10 Conservative never sets restartCycle", conservativeRestarts == 0);
    expect("F01-T10 Creative never sets restartCycle", creativeRestarts == 0);
    expect("F01-T10 Experimental sets restartCycle sometimes", experimentalRestarts > 0);
}

// F02-T02 / F02-T03: morph of effect settings.
void morphSettings()
{
    VariationSettings a = plainSource();
    VariationSettings b = plainSource();
    a.operations[0] = {true, 0.2f, 0.25f};
    b.operations[0] = {true, 0.6f, 1.0f};
    const auto half = morph(a, b, 0.5f);
    expect("F02-T02 two-sided depth is linear", near(half.operations[0].depth, 0.4f));
    expect("F02-T02 slice is log2-linear (geometric midpoint)", near(half.operations[0].sliceBeats, 0.5f, 1.0e-4f));
    const auto quarter = morph(a, b, 0.25f);
    expect("F02-T02 quarter weight depth", near(quarter.operations[0].depth, 0.3f));
    expect("F02-T02 quarter weight slice", near(quarter.operations[0].sliceBeats, 0.25f * std::pow(4.0f, 0.25f), 1.0e-4f));

    const float nan = std::nanf("");
    const float inf = INFINITY;
    expect("F02-T03 NaN weight is 0", near(morph(a, b, nan).operations[0].depth, 0.2f));
    expect("F02-T03 negative weight is 0", near(morph(a, b, -3.0f).operations[0].depth, 0.2f));
    expect("F02-T03 weight above 1 clamps to 1", near(morph(a, b, 7.0f).operations[0].depth, 0.6f));
    expect("F02-T03 infinite weight is safe", std::isfinite(morph(a, b, inf).operations[0].depth)
                                                  && std::isfinite(morph(a, b, -inf).operations[0].depth));
    bool finite = true;
    for (float w : {nan, inf, -inf, 0.0f, 0.3f, 1.0f, 1.0e30f})
        for (const auto& op : morph(a, b, w).operations)
            finite = finite && std::isfinite(op.depth) && std::isfinite(op.sliceBeats) && op.depth >= 0.0f && op.depth <= 1.0f;
    expect("F02-T03 every weight yields finite, bounded settings", finite);
}

// F03-T10: the downbeat protection span is capped at one bar.
void protectSpan()
{
    {
        SceneControl c;
        c.placement.downbeatProtectBeats = 4.0f;
        const GridContext three{3, 4}; // 3/4: bar = 3 meter beats
        bool allSilent = true;
        for (double pos = 0.0; pos < 12.0; pos += 0.25)
            allSilent = allSilent && !evaluate(c, three, pos).active[0];
        expect("F03-T10 protect 4 beats in a 3-beat bar silences the whole bar", allSilent);
    }
    {
        SceneControl c;
        c.placement.downbeatProtectBeats = 4.0f;
        const GridContext seven{7, 2}; // 7/8 counted in meter beats: bar = 7 beats
        expect("F03-T10 7-beat bar: first 4 beats silent", !evaluate(c, seven, 0.0).active[0] && !evaluate(c, seven, 3.75).active[0]);
        expect("F03-T10 7-beat bar: beat 5 active", evaluate(c, seven, 4.0).active[0] && evaluate(c, seven, 6.5).active[0]);
        expect("F03-T10 7-beat bar: next bar start silent again", !evaluate(c, seven, 7.0).active[0] && !evaluate(c, seven, 10.5).active[0]);
        expect("F03-T10 boundary distance reaches the protect edge", std::abs(evaluate(c, seven, 3.5).ticksUntilChange - 0.5 * ticksPerBeat) < 1.0e-6);
    }
}

// F06-T04 / T05 / T06: polyrhythmic lane rings.
void polyrhythm()
{
    // T04: loop lengths above 64 are clamped by the loader and round-trip below that.
    scene::SceneBank bank;
    auto v = bank.variation(0);
    v.settings.control.lane.loopBeats[2] = 7;
    bank.setVariation(0, v);
    auto text = scene::SceneSerializer::serialize(bank);
    const std::string key = "variation.0.lane.loop.2=7";
    expect("F06-T04 loop length is serialized", text.find(key) != std::string::npos);
    auto pos = text.find(key);
    if (pos != std::string::npos)
        text.replace(pos, key.size(), "variation.0.lane.loop.2=200");
    const auto loaded = scene::SceneSerializer::deserialize(text);
    expect("F06-T04 value above 64 is clamped to 64", loaded.bank.variation(0).settings.control.lane.loopBeats[2] == 64);

    const GridContext grid{4, 4};
    SceneControl c;
    c.lane.explicitEvents = true;
    c.lane.loopBeats[2] = 3;
    LaneEvent e;
    e.operation = 2;
    e.onsetTicks = 0;
    e.durationTicks = ticksPerBeat;
    expect("F06-T05 add looped event", addEvent(c.lane, e, grid.phraseTicks()) == EditStatus::Ok);
    expect("F06-T05 negative position wraps onto the ring (active)", evaluate(c, grid, -2.5).active[2]);
    expect("F06-T05 negative position wraps onto the ring (silent)", !evaluate(c, grid, -0.5).active[2]);
    expect("F06-T05 negative and positive agree one ring apart",
           evaluate(c, grid, -1.5).active[2] == evaluate(c, grid, 1.5).active[2]
               && evaluate(c, grid, -3.5).active[2] == evaluate(c, grid, 2.5).active[2]);

    // T06: restartCycle restarts the engine grid at every repetition of the ring.
    SceneControl r;
    r.lane.explicitEvents = true;
    r.lane.loopBeats[2] = 3;
    LaneEvent re = e;
    re.restartCycle = true;
    expect("F06-T06 add restart event", addEvent(r.lane, re, grid.phraseTicks()) == EditStatus::Ok);
    bool restarts = true;
    for (int rep = 0; rep < 5; ++rep)
    {
        const auto t = evaluate(r, grid, rep * 3 + 0.5);
        restarts = restarts && t.active[2] && std::abs(t.phaseOffsetBeats[2] - rep * 3.0) < 1.0e-9;
    }
    expect("F06-T06 each repetition restarts the grid at its own onset", restarts);
}

// F11-T06 / F11-T07: gesture playback write threshold and phrase wrap.
void gestureLane()
{
    const float nan = std::nanf("");
    expect("F11-T06 a change of 1/1000 is not written", !GestureLane::worthWriting(0.5f, 0.501f));
    expect("F11-T06 a change of exactly 1/512 is not written", !GestureLane::worthWriting(0.5f, 0.5f + 1.0f / 512.0f));
    expect("F11-T06 a change of 1/100 is written", GestureLane::worthWriting(0.5f, 0.51f));
    expect("F11-T06 downward changes count the same", GestureLane::worthWriting(0.5f, 0.49f) && !GestureLane::worthWriting(0.5f, 0.499f));
    expect("F11-T06 nothing recorded (negative sample) is never written", !GestureLane::worthWriting(0.5f, -1.0f));
    expect("F11-T06 NaN is never written", !GestureLane::worthWriting(0.5f, nan) && !GestureLane::worthWriting(nan, 0.5f));
    expect("F11-T06 out-of-range is never written", !GestureLane::worthWriting(0.5f, 1.5f));

    GestureLane lane;
    lane.record(0.0, 0.0f);
    lane.record(0.5, 1.0f);
    const float justBefore = lane.sample(0.9999);
    const float atStart = lane.sample(0.0);
    expect("F11-T07 the lane is continuous across the phrase wrap", std::abs(justBefore - atStart) < 0.01f);
    expect("F11-T07 positions beyond one phrase wrap", std::abs(lane.sample(1.25) - lane.sample(0.25)) < 1.0e-5f);
    expect("F11-T07 negative positions wrap", std::abs(lane.sample(-0.75) - lane.sample(0.25)) < 1.0e-5f);
}
} // namespace

int main()
{
    gestureLane();
    goldenCandidate();
    generationCapacity();
    styleRestart();
    morphSettings();
    protectSpan();
    polyrhythm();
    if (failures == 0)
        std::cout << "CompletenessTests passed\n";
    return failures == 0 ? 0 : 1;
}
