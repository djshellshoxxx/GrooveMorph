// W3 core tests: capacity (W3-CAP), loop-aware Reduce (W3-RED), Dry return (W3-DR), launch phase capture (W3-PC),
// input sanitizer (W3-NF), C04 smoothing sanitize/serialization (W3-C04S).
#include "core/control/AutoReturn.h"
#include "core/control/LaunchPhase.h"
#include "core/control/PlacementEvaluator.h"
#include "core/control/VariationBlend.h"
#include "core/dsp/InputSanitizer.h"
#include "core/dsp/RhythmEngine.h"
#include "core/scene/SceneBank.h"
#include "core/scene/SceneSerializer.h"
#include "core/variation/CandidateBuilder.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::control;

namespace {
int failures = 0;
void expect(const std::string& name, bool ok) {
    if (!ok) { std::cerr << name << ": check failed\n"; ++failures; }
}

constexpr std::int64_t P = 4 * 4 * ticksPerBeat; // 4/4, 4 bars

LaneEvent ev(std::uint8_t op, int onset, int dur, std::uint8_t prio = 50) {
    LaneEvent e; e.operation = op; e.onsetTicks = onset; e.durationTicks = dur; e.priority = prio; return e;
}

// Reference implementation of the PRE-W3 greedy (phrase ring only), kept to prove "no loops = identical decisions".
struct OldReport { std::vector<bool> kept; };
OldReport oldReduce(const EventLane& lane, const ActivityConfig& cfg, std::int64_t phraseTicks) {
    const auto circ = [&](std::int64_t a, std::int64_t b) {
        std::int64_t d = (a - b) % phraseTicks; if (d < 0) d += phraseTicks; return std::min(d, phraseTicks - d);
    };
    const std::int64_t spacing = static_cast<std::int64_t>(std::ceil(static_cast<double>(cfg.minSpacingBeats) * ticksPerBeat));
    const std::int64_t limit = static_cast<std::int64_t>(std::floor(static_cast<double>(cfg.maxCoverage) * static_cast<double>(phraseTicks)));
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < lane.count; ++i) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return lane.events[a].priority > lane.events[b].priority; });
    const auto unionOf = [&](const std::vector<bool>& set) {
        std::vector<bool> covered(static_cast<std::size_t>(phraseTicks), false);
        for (std::size_t i = 0; i < lane.count; ++i)
            if (set[i] && lane.events[i].enabled)
                for (std::int64_t t = 0; t < lane.events[i].durationTicks; ++t)
                    covered[static_cast<std::size_t>((lane.events[i].onsetTicks + t) % phraseTicks)] = true;
        std::int64_t n = 0; for (bool c : covered) n += c ? 1 : 0; return n;
    };
    OldReport r; r.kept.assign(lane.count, false);
    for (auto i : order) {
        bool bad = false;
        if (spacing > 0)
            for (std::size_t j = 0; j < lane.count && !bad; ++j)
                bad = r.kept[j] && lane.events[j].enabled && lane.events[i].enabled && circ(lane.events[j].onsetTicks, lane.events[i].onsetTicks) < spacing;
        if (!bad) { auto t = r.kept; t[i] = true; bad = unionOf(t) > limit; }
        if (!bad) r.kept[i] = true;
    }
    return r;
}
} // namespace

int main() {
    // ---- W3-CAP ----
    {
        EventLane lane;
        int added = 0;
        for (int i = 0; i < 260; ++i)
            if (addEvent(lane, ev(static_cast<std::uint8_t>(i % 7), (i / 7) * 100, 50), P) == EditStatus::Ok) ++added;
        expect("W3-CAP-T01 256 events fit", added == 256 && lane.count == 256 && maxLaneEvents == 256);
        expect("W3-CAP-T01 257th is Capacity", addEvent(lane, ev(0, 6000, 10), P) == EditStatus::Capacity);
        expect("W3-CAP-T01 count type holds 256", std::is_same_v<decltype(lane.count), std::uint16_t>);
        expect("W3-CAP-T07 eight regions", maxProtectedRegions == 8);
        expect("W3-CAP-T06 snapshot trivially copyable", std::is_trivially_copyable_v<scene::SceneSnapshot>);
        expect("W3-CAP-T06 snapshot under 64 KiB", sizeof(scene::SceneSnapshot) <= 64 * 1024);
    }
    {
        // Serialization: full bank with 8 regions and 256 events per scene.
        scene::SceneBank bank;
        for (std::size_t v = 0; v < variationCount; ++v) {
            auto s = bank.variation(v).settings;
            for (int i = 0; i < 256; ++i) {
                LaneEvent e = ev(static_cast<std::uint8_t>(i % 7), (i / 7) * 100, 50, static_cast<std::uint8_t>(i % 100));
                (void) addEvent(s.control.lane, e, P);
            }
            s.control.lane.explicitEvents = true;
            s.control.placement.regionCount = 8;
            for (int r = 0; r < 8; ++r) s.control.placement.regions[static_cast<std::size_t>(r)] = {r * 1000, 100 + r};
            bank.setVariationSettings(v, s);
        }
        const auto text = scene::SceneSerializer::serialize(bank);
        expect("W3-CAP-T02 serialized size < 400 KB", text.size() < 400000);
        const auto back = scene::SceneSerializer::deserialize(text);
        expect("W3-CAP-T02 full bank round trips", back.bank == bank && back.bank.variation(3).settings.control.lane.count == 256
                                                    && back.bank.variation(0).settings.control.placement.regionCount == 8);
        expect("W3-CAP-T02 loads without recovery", back.report.status == scene::LoadStatus::Ok);
        // Out-of-capacity keys clamp, never crash.
        std::string tooMany = "groovemorph-scenes v1\nvariation.0.placement.regions=9\nvariation.0.event.300.op=1\nvariation.0.event.300.onset=0\nvariation.0.event.300.duration=10\nvariation.0.region.9.start=5\n";
        const auto clamped = scene::SceneSerializer::deserialize(tooMany);
        expect("W3-CAP-T03 oversize keys clamped", clamped.bank.variation(0).settings.control.placement.regionCount <= 8
                                                     && clamped.bank.variation(0).settings.control.lane.count == 0);
        // Legacy alpha fixture (32 events, 4 regions) loads identically.
        scene::SceneBank legacy;
        auto ls = legacy.variation(0).settings;
        for (int i = 0; i < 32; ++i) (void) addEvent(ls.control.lane, ev(static_cast<std::uint8_t>(i % 5), (i / 5) * 200, 100), P);
        ls.control.lane.explicitEvents = true;
        ls.control.placement.regionCount = 4;
        for (int r = 0; r < 4; ++r) ls.control.placement.regions[static_cast<std::size_t>(r)] = {r * 960, 480};
        legacy.setVariationSettings(0, ls);
        expect("W3-CAP-T03 legacy-size file round trips", scene::SceneSerializer::deserialize(scene::SceneSerializer::serialize(legacy)).bank == legacy);
    }
    {
        // evaluate() with 256 events against brute force.
        SceneControl c;
        c.lane.explicitEvents = true;
        for (int i = 0; i < 256; ++i) (void) addEvent(c.lane, ev(static_cast<std::uint8_t>(i % 7), (i / 7) * 400, 150 + (i % 5) * 20), P);
        GridContext grid;
        bool ok = true;
        for (int k = 0; k < 1000; ++k) {
            const double pos = (k * 37 % 15360) / 960.0 + 0.001 * (k % 3);
            const auto t = evaluate(c, grid, pos, 0x7fu);
            const double tick = std::fmod(pos * ticksPerBeat, static_cast<double>(P));
            for (std::size_t op = 0; op < 7; ++op) {
                bool want = false;
                for (std::size_t i = 0; i < c.lane.count; ++i)
                    if (c.lane.events[i].operation == op && tick >= c.lane.events[i].onsetTicks && tick < c.lane.events[i].onsetTicks + c.lane.events[i].durationTicks) want = true;
                ok = ok && t.active[op] == want;
            }
        }
        expect("W3-CAP-T04 evaluator matches brute force with 256 events", ok);
    }
    {
        // Other operations on a full lane.
        VariationSettings a;
        for (int i = 0; i < 256; ++i) (void) addEvent(a.control.lane, ev(static_cast<std::uint8_t>(i % 7), (i / 7) * 400, 100, static_cast<std::uint8_t>(i % 90)), P);
        a.control.lane.explicitEvents = true;
        a.control.lane.events[3].locked = true;
        ActivityConfig cfg; cfg.maxCoverage = 0.2f;
        EventLane copy = a.control.lane;
        const auto r = reduceEvents(copy, cfg, a.control.permissions, P, true);
        expect("W3-CAP-T05 reduce on a full lane", r.status == ReduceStatus::Valid && copy.count == r.kept && copy.count <= 256 && r.dropped > 0);
        bool lockedKept = false;
        for (std::size_t i = 0; i < copy.count; ++i) lockedKept = lockedKept || copy.events[i].locked;
        expect("W3-CAP-T05 locked event kept", lockedKept);
        EventLane sc = a.control.lane;
        const auto rs = scaleEvents(sc, 1, 2, P, a.control.permissions);
        expect("W3-CAP-T05 scale on a full lane", sc.count <= 256 && rs.scaled + rs.skippedLocked + rs.dropped == 256);
        VariationSettings cand = a;
        cand.control.lane.count = 0;
        const auto restored = enforcePermissions(a, cand);
        (void) restored;
        expect("W3-CAP-T05 enforcePermissions bounded", cand.control.lane.count <= 256);
    }

    // ---- W3-RED ----
    {
        // T01: no loops, randomized corpus, same decisions as the pre-W3 greedy.
        std::uint32_t seed = 12345;
        const auto rnd = [&](int n) { seed = seed * 1664525u + 1013904223u; return static_cast<int>((seed >> 8) % static_cast<unsigned>(n)); };
        bool same = true;
        for (int round = 0; round < 500 && same; ++round) {
            EventLane lane;
            const int n = 1 + rnd(10);
            for (int i = 0; i < n; ++i)
                (void) addEvent(lane, ev(static_cast<std::uint8_t>(rnd(7)), rnd(15000), 1 + rnd(2500), static_cast<std::uint8_t>(rnd(101))), P);
            ActivityConfig cfg;
            cfg.minSpacingBeats = rnd(4) * 0.25f;
            cfg.maxCoverage = 0.1f + 0.1f * rnd(10);
            const auto want = oldReduce(lane, cfg, P);
            EventLane work = lane;
            const auto got = reduceEvents(work, cfg, PermissionConfig{}, P, true);
            std::vector<LaneEvent> keptWant;
            for (std::size_t i = 0; i < lane.count; ++i) if (want.kept[i]) keptWant.push_back(lane.events[i]);
            same = got.status == ReduceStatus::Valid && work.count == keptWant.size();
            for (std::size_t i = 0; same && i < work.count; ++i)
                same = work.events[i].onsetTicks == keptWant[i].onsetTicks && work.events[i].operation == keptWant[i].operation;
        }
        expect("W3-RED-T01 no loops: decisions identical to the old greedy (500 cases)", same);
    }
    {
        // T02: spacing across rings. Gate (op 2) loops every 3 beats, Repeat (op 0) every 4; closest approach is 0 beats.
        EventLane lane;
        lane.loopBeats[2] = 3; lane.loopBeats[0] = 4;
        lane.events[0] = ev(2, 0, 100, 80);
        lane.events[1] = ev(0, 960, 100, 20);
        lane.count = 2;
        ActivityConfig cfg; cfg.minSpacingBeats = 0.5f;
        EventLane work = lane;
        const auto r = reduceEvents(work, cfg, PermissionConfig{}, P, true);
        // gcd(2880, 3840) = 960: onsets 0 and 960 are 960 ticks apart modulo 960 -> distance 0.
        expect("W3-RED-T02 gcd spacing drops the lower priority event", r.kept == 1 && r.dropped == 1 && work.events[0].operation == 2);
        cfg.minSpacingBeats = 0.0f;
        EventLane keepAll = lane;
        expect("W3-RED-T02 no spacing keeps both", reduceEvents(keepAll, cfg, PermissionConfig{}, P, true).kept == 2);
    }
    {
        // T03: self repetition shorter than the spacing.
        EventLane lane;
        lane.loopBeats[2] = 1;
        lane.events[0] = ev(2, 0, 100);
        lane.count = 1;
        ActivityConfig cfg; cfg.minSpacingBeats = 2.0f;
        EventLane work = lane;
        const auto r = reduceEvents(work, cfg, PermissionConfig{}, P, true);
        expect("W3-RED-T03 self repetition closer than spacing drops the event", r.dropped == 1 && work.count == 0);
        PermissionConfig perm; perm.operationLocks[2] = lock::events;
        EventLane locked = lane;
        const auto rl = reduceEvents(locked, cfg, perm, P, true);
        expect("W3-RED-T03 locked: ConflictLocked, lane untouched", rl.status == ReduceStatus::ConflictLocked && locked.count == 1);
    }
    {
        // T04: coverage of a looping event: 1 beat every 2 beats in a 16-beat phrase = 50 %.
        EventLane lane;
        lane.loopBeats[1] = 2;
        lane.events[0] = ev(1, 0, 960);
        lane.count = 1;
        ActivityConfig cfg; cfg.maxCoverage = 1.0f;
        EventLane work = lane;
        const auto r = reduceEvents(work, cfg, PermissionConfig{}, P, false);
        expect("W3-RED-T04 looping coverage is 50 %", std::abs(r.coverage - 0.5f) < 1e-6f && r.kept == 1);
        cfg.maxCoverage = 0.4f;
        const auto r2 = reduceEvents(work, cfg, PermissionConfig{}, P, true);
        expect("W3-RED-T04 cap 40 % drops it", r2.dropped == 1 && work.count == 0);
    }
    {
        // T05: peak concurrency across 3 and 4 beat rings: both are active at beat 12 (onset 0 of each).
        EventLane lane;
        lane.loopBeats[0] = 3; lane.loopBeats[1] = 4;
        lane.events[0] = ev(0, 0, 480);
        lane.events[1] = ev(1, 0, 480);
        lane.count = 2;
        EventLane work = lane;
        const auto r = reduceEvents(work, ActivityConfig{}, PermissionConfig{}, P, false);
        expect("W3-RED-T05 loop alignment gives peak 2", r.peakConcurrent == 2);
        EventLane apart = lane;
        apart.events[1].onsetTicks = 600;
        apart.loopBeats[1] = 0; apart.loopBeats[0] = 0;
        expect("W3-RED-T05 disjoint events give peak 1", reduceEvents(apart, ActivityConfig{}, PermissionConfig{}, P, false).peakConcurrent == 1);
    }
    {
        // T06: wrap on the ring.
        EventLane lane;
        lane.loopBeats[3] = 4;
        lane.events[0] = ev(3, 3 * 960, 2 * 960);
        lane.count = 1;
        ActivityConfig cfg;
        EventLane work = lane;
        const auto r = reduceEvents(work, cfg, PermissionConfig{}, P, false);
        // [3,5) on a 4-beat ring covers [3,4) and [0,1): 2 beats per 4-beat ring = 50 %.
        expect("W3-RED-T06 wrapped looping event covers half the phrase", std::abs(r.coverage - 0.5f) < 1e-6f);
    }
    {
        // T07: determinism with 256 events and 8 loops.
        EventLane lane;
        for (int i = 0; i < 256; ++i) (void) addEvent(lane, ev(static_cast<std::uint8_t>(i % 7), (i / 7) * 200, 100, static_cast<std::uint8_t>((i * 7) % 100)), 100000);
        for (std::size_t op = 0; op < 7; ++op) lane.loopBeats[op] = static_cast<std::uint8_t>(40 + op);
        ActivityConfig cfg; cfg.minSpacingBeats = 0.25f; cfg.maxCoverage = 0.6f;
        EventLane a = lane, b = lane;
        const auto ra = reduceEvents(a, cfg, PermissionConfig{}, P, true), rb = reduceEvents(b, cfg, PermissionConfig{}, P, true);
        bool same = ra.kept == rb.kept && a.count == b.count;
        for (std::size_t i = 0; same && i < a.count; ++i) same = a.events[i].onsetTicks == b.events[i].onsetTicks;
        expect("W3-RED-T07 256 events with loops: deterministic", same && ra.kept + ra.dropped == 256);
    }

    // ---- W3-DR ----
    {
        AutoReturn r;
        r.armDry(1.3, 4);
        expect("W3-DR-T01 armed dry", r.armed() && r.isDry() && !r.target().has_value() && !r.requestPending());
        const double spb = 96000.0;
        expect("W3-DR-T01 offset from 4.5", r.dryBoundaryOffset(4.5, spb) == static_cast<std::int64_t>(0.5 * spb));
        expect("W3-DR-T01 offset from 4.999", r.dryBoundaryOffset(4.999, spb) == 96);
        expect("W3-DR-T01 offset at 5.0 is 0", r.dryBoundaryOffset(5.0, spb) == 0);
        expect("W3-DR-T01 offset past 5.2 is 0", r.dryBoundaryOffset(5.2, spb) == 0);
        expect("W3-DR-T01 never requests a scene", !r.poll(4.5).has_value() && !r.poll(6.0).has_value());
        r.disarm();
        expect("W3-DR-T01 disarm clears", !r.isDry() && !r.dryBoundaryOffset(4.5, spb).has_value());
        r.armDry(std::numeric_limits<double>::quiet_NaN(), 4);
        expect("W3-DR-T01 NaN position disarms", !r.armed());
        r.armDry(1.0, 0);
        expect("W3-DR-T01 zero bars disarms", !r.armed());
        AutoReturn s;
        s.arm(2.1, 2, 1);
        expect("W3-DR-T02 scene arming is not dry", s.armed() && !s.isDry() && s.target() == 1u && !s.dryBoundaryOffset(3.0, 96000.0).has_value());
        expect("W3-DR-T02 scene poll unchanged", s.poll(3.5) == 1u);
    }

    // ---- W3-PC ----
    {
        LaunchPhase lp;
        PhaseConfig captured; captured.policy = PhasePolicy::Restart; captured.offsetBeats = 1.0f;
        PhaseConfig fallback; fallback.policy = PhasePolicy::Preserve;
        lp.capture(1, captured);
        const auto got = lp.take(1, fallback);
        expect("W3-PC-T01 take returns the captured config", got.policy == PhasePolicy::Restart && got.offsetBeats == 1.0f);
        expect("W3-PC-T01 slot cleared", !lp.has() && lp.take(1, fallback).policy == PhasePolicy::Preserve);
        lp.capture(1, captured);
        expect("W3-PC-T02 other variation gets the fallback", lp.take(2, fallback).policy == PhasePolicy::Preserve && !lp.has());
        PhaseConfig bad; bad.offsetBeats = std::numeric_limits<float>::quiet_NaN(); bad.smoothingMs = 1.0e9f;
        lp.capture(0, bad);
        const auto s = lp.take(0, fallback);
        expect("W3-PC-T03 captured config is sanitized", s.offsetBeats == 0.0f && s.smoothingMs == 250.0f);
    }

    // ---- W3-NF ----
    {
        std::vector<float> l{0.5f, std::nanf(""), INFINITY, -INFINITY, 1.0e-40f, -0.25f};
        std::vector<float> r = l;
        float* ch[] = {l.data(), r.data(), nullptr};
        const int n = dsp::sanitizeBlock(ch, 3, static_cast<int>(l.size()));
        expect("W3-NF-T01 counts replacements", n == 6);
        expect("W3-NF-T01 finite values untouched", l[0] == 0.5f && l[4] == 1.0e-40f && l[5] == -0.25f && l[1] == 0.0f && l[2] == 0.0f && l[3] == 0.0f);
        expect("W3-NF-T01 null/empty safe", dsp::sanitizeBlock(nullptr, 2, 10) == 0 && dsp::sanitizeBlock(ch, 2, 0) == 0);
    }
    {
        // T02: the engine keeps its history finite when it sees garbage (defence in depth behind the sanitizer).
        RhythmEngine a, b;
        a.prepare(48000.0, 256, 2);
        b.prepare(48000.0, 256, 2);
        VariationSettings v;
        for (std::size_t i = 0; i < operationCount; ++i) v.operations[i] = {true, 1.0f, 0.25f};
        RhythmTiming timing{4800.0, 0.0, true};
        std::vector<float> clean(256, 0.3f), dirty(256, 0.3f);
        dirty[10] = std::nanf(""); dirty[11] = INFINITY;
        bool finite = true;
        for (int blk = 0; blk < 200; ++blk) {
            auto x = (blk == 3 ? dirty : clean);
            auto y = clean;
            float* cx[] = {x.data(), x.data()};
            float* cy[] = {y.data(), y.data()};
            timing.beatAtBlockStart = blk * 256.0 / 4800.0;
            a.process(cx, 2, 256, v, 1.0f, timing);
            b.process(cy, 2, 256, v, 1.0f, timing);
            for (float s : x) finite = finite && std::isfinite(s);
        }
        expect("W3-NF-T02 engine output stays finite after garbage", finite);
    }

    // ---- W3-C04S (core part) ----
    {
        PhaseConfig p; p.smoothingMs = -5.0f;
        expect("W3-C04S-T06 negative clamps", sanitize(p) && p.smoothingMs == 0.0f);
        p.smoothingMs = std::numeric_limits<float>::quiet_NaN();
        expect("W3-C04S-T06 NaN to 0", sanitize(p) && p.smoothingMs == 0.0f);
        p.smoothingMs = 1.0e9f;
        expect("W3-C04S-T06 huge clamps to 250", sanitize(p) && p.smoothingMs == 250.0f);
        scene::SceneBank bank;
        auto s = bank.variation(1).settings;
        s.control.phase.smoothingMs = 40.0f;
        bank.setVariationSettings(1, s);
        const auto text = scene::SceneSerializer::serialize(bank);
        expect("W3-C04S-T06 key written when non-zero", text.find("variation.1.phase.smooth=") != std::string::npos);
        expect("W3-C04S-T06 round trip", scene::SceneSerializer::deserialize(text).bank.variation(1).settings.control.phase.smoothingMs == 40.0f);
        expect("W3-C04S-T06 default scene writes no smooth key", scene::SceneSerializer::serialize(scene::SceneBank{}).find("phase.smooth") == std::string::npos);
        expect("W3-C04S-T06 absent key loads 0", scene::SceneSerializer::deserialize("groovemorph-scenes v1\nvariation.0.phase.policy=1\n").bank.variation(0).settings.control.phase.smoothingMs == 0.0f);
    }

    if (failures == 0) std::cout << "W3ControlTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
