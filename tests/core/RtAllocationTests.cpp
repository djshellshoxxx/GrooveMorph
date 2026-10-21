// A3 RT audit: counting-allocator hook. This executable replaces the global operator new/delete and
// asserts that the portable audio-path code performs ZERO heap allocations (and no deallocations) after
// its prepare()/arm() step. See docs/qa/RT-AUDIT-BETA.md.
//
// The counter is thread-local armed, so helper threads and the test harness itself are never counted.
#include "core/MidiLinks.h"
#include "core/Scheduler.h"
#include "core/TransportResolver.h"
#include "core/control/AutoReturn.h"
#include "core/control/ChaosMapper.h"
#include "core/control/Chance.h"
#include "core/control/LaunchPhase.h"
#include "core/control/OperationTrims.h"
#include "core/control/PhaseResolver.h"
#include "core/control/PlacementEvaluator.h"
#include "core/control/VariationBlend.h"
#include "core/dsp/Crossover.h"
#include "core/dsp/DemoSource.h"
#include "core/dsp/InputSanitizer.h"
#include "core/dsp/LoopSource.h"
#include "core/dsp/OnsetDetector.h"
#include "core/dsp/OutputCapture.h"
#include "core/dsp/RhythmEngine.h"
#include "core/scene/MidiNoteMap.h"
#include "core/scene/SceneBank.h"
#include "core/trace/TraceRing.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
std::atomic<std::size_t> g_allocations{0};
std::atomic<std::size_t> g_deallocations{0};
thread_local bool t_counting = false;

void* countedAlloc(std::size_t n)
{
    if (t_counting)
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n != 0 ? n : 1);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void countedFree(void* p) noexcept
{
    if (p != nullptr && t_counting)
        g_deallocations.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
} // namespace

void* operator new(std::size_t n) { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    try { return countedAlloc(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept
{
    try { return countedAlloc(n); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { countedFree(p); }

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

// RAII region in which every allocation / deallocation on this thread is counted.
class NoAllocRegion
{
public:
    NoAllocRegion() noexcept
    {
        g_allocations.store(0);
        g_deallocations.store(0);
        t_counting = true;
    }
    ~NoAllocRegion() { t_counting = false; }
    [[nodiscard]] std::size_t allocations() const noexcept { return g_allocations.load(); }
    [[nodiscard]] std::size_t deallocations() const noexcept { return g_deallocations.load(); }
};

void expectNoAlloc(std::string_view name, const NoAllocRegion& region)
{
    if (region.allocations() != 0 || region.deallocations() != 0)
        std::cerr << name << ": " << region.allocations() << " allocations, " << region.deallocations()
                  << " deallocations on the audio path\n";
    expect(name, region.allocations() == 0 && region.deallocations() == 0);
}

struct Rng
{
    std::uint64_t s = 0x1234abcd5678ef01ull;
    std::uint64_t next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    float unit() noexcept { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }
    float noise() noexcept { return unit() * 2.0f - 1.0f; }
};

constexpr std::int64_t phraseTicks = 4 * 4 * ticksPerBeat;

// A deliberately heavy scene: every operation on, a full explicit lane (36 events per operation, 252 in
// total), chance, loop lengths, regions, activity limit, downbeat protection and custom placement.
VariationSettings heavyScene()
{
    VariationSettings v;
    for (std::size_t op = 0; op < operationCount; ++op)
        v.operations[op] = {true, 0.8f, 0.125f + 0.0625f * static_cast<float>(op)};
    auto& c = v.control;
    c.placement.enabled = true;
    c.placement.window = WindowMode::Custom;
    c.placement.startPhase = 0.125f;
    c.placement.spanPhase = 0.75f;
    c.placement.every = 2;
    c.placement.downbeatProtectBeats = 0.25f;
    c.placement.regionCount = static_cast<std::uint8_t>(maxProtectedRegions);
    for (std::size_t r = 0; r < maxProtectedRegions; ++r)
        c.placement.regions[r] = {static_cast<std::int32_t>(r * 1800 + 100), 120};
    c.lane.explicitEvents = true;
    for (std::size_t op = 0; op < laneCount; ++op)
    {
        c.lane.chanceByOp[op] = static_cast<std::uint8_t>(70 + op);
        for (int k = 0; k < 36; ++k)
        {
            LaneEvent e;
            e.operation = static_cast<std::uint8_t>(op);
            e.onsetTicks = static_cast<std::int32_t>(k * 420);
            e.durationTicks = 300;
            e.depth = 0.6f;
            e.chance = static_cast<std::uint8_t>(40 + k);
            e.restartCycle = (k % 5) == 0;
            e.priority = static_cast<std::uint8_t>(10 * op);
            if (addEvent(c.lane, e, phraseTicks) != EditStatus::Ok)
                std::cerr << "heavyScene: addEvent failed op " << op << " k " << k << "\n";
        }
    }
    for (std::size_t op = 0; op < laneCount; ++op) // set after adding: loops re-fold the events onto a shorter ring
        c.lane.loopBeats[op] = (op % 3 == 0) ? static_cast<std::uint8_t>(0) : static_cast<std::uint8_t>(3 + op);
    c.lane.chanceSeed = 0x9e3779b9u;
    c.activity.enabled = true;
    c.activity.maxConcurrent = 3;
    return v;
}

struct TimingCase
{
    const char* name;
    RhythmTiming timing;
};

std::vector<TimingCase> timingCases()
{
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();
    std::vector<TimingCase> out;
    RhythmTiming t;
    t.samplesPerBeat = 24000.0;
    t.isPlaying = true;
    t.beatAtBlockStart = 0.0;
    out.push_back({"playing", t});
    t.beatAtBlockStart = -37.25;
    out.push_back({"negative beat", t});
    t.beatAtBlockStart = 1.0e12;
    out.push_back({"huge beat", t});
    t.beatAtBlockStart = 3.0;
    t.samplesPerBeat = 1.0e-3;
    out.push_back({"tiny samples per beat", t});
    t.samplesPerBeat = 1.0e12;
    out.push_back({"huge samples per beat", t});
    t.samplesPerBeat = nan;
    out.push_back({"nan samples per beat", t});
    t.samplesPerBeat = inf;
    out.push_back({"inf samples per beat", t});
    t.samplesPerBeat = 24000.0;
    t.beatAtBlockStart = nan;
    out.push_back({"nan beat", t});
    t.beatAtBlockStart = 5.0;
    t.isPlaying = false;
    out.push_back({"stopped", t});
    t.isPlaying = true;
    t.crossfadeSeconds = 0.2;
    t.phaseOffsetBeats.fill(1.5);
    out.push_back({"long crossfade, phase offsets", t});
    t.crossfadeSeconds = nan;
    t.phaseOffsetBeats.fill(-1.0e9);
    out.push_back({"nan crossfade, hostile offsets", t});
    return out;
}

void testEngine()
{
    constexpr int block = 256;
    RhythmEngine engine;
    trace::TraceRing ring; // allocated here (message thread); the engine only pushes into it
    ring.setActive(true);
    engine.prepare(48000.0, block, 2);
    engine.setTraceSink(&ring);
    const auto scene = heavyScene();
    std::vector<float> l(block), r(block);
    float* ch[2] = {l.data(), r.data()};
    Rng rng;
    bool finite = true;

    const auto cases = timingCases();
    const NoAllocRegion region;
    for (int pass = 0; pass < 3; ++pass)
        for (const auto& c : cases)
        {
            auto timing = c.timing;
            for (int b = 0; b < 40; ++b)
            {
                for (int i = 0; i < block; ++i)
                {
                    l[static_cast<std::size_t>(i)] = rng.noise();
                    r[static_cast<std::size_t>(i)] = rng.noise();
                }
                if (std::isfinite(timing.beatAtBlockStart) && std::isfinite(timing.samplesPerBeat)
                    && timing.samplesPerBeat > 0.0 && timing.samplesPerBeat < 1.0e6)
                    timing.beatAtBlockStart += block / timing.samplesPerBeat;
                engine.process(ch, 2, block, scene, pass == 1 ? 0.5f : 1.0f, timing);
                if (b == 20)
                    engine.resetHistory();
                if (b == 30)
                    engine.startTraceEpoch();
                for (int i = 0; i < block; ++i)
                    finite = finite && std::isfinite(l[static_cast<std::size_t>(i)])
                             && std::isfinite(r[static_cast<std::size_t>(i)]);
            }
        }
    expectNoAlloc("RhythmEngine::process allocates nothing (all ops, hostile timing, tracing)", region);
    expect("RhythmEngine output finite for hostile timing", finite);
}

void testPlacementChaosMorph()
{
    const auto a = heavyScene();
    auto b = heavyScene();
    b.control.lane.chanceSeed = 77;
    for (auto& e : b.control.lane.events)
        e.onsetTicks = (e.onsetTicks + 60) % static_cast<std::int32_t>(phraseTicks);
    b.operations[2].enabled = false;
    OperationTrims trims{};
    trims[1].depth = 0.5f;
    trims[3].slice = 2.0f;
    trims[4].enable = EnableTrim::ForceOff;

    GridContext grid;
    VariationSettings sink;
    double acc = 0.0;
    const NoAllocRegion region;
    for (double pos = -20.0; pos < 40.0; pos += 0.0137)
    {
        const auto t = evaluate(a.control, grid, pos, 0x7fu, pos - 1.0);
        acc += t.ticksUntilChange + static_cast<double>(t.active[0]);
    }
    for (const double pos : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(), 1.0e300, -1.0e300, 4.0e18, 1.0e12})
    {
        const auto t = evaluate(a.control, grid, pos, 0x7fu, pos);
        acc += t.ticksUntilChange;
        const auto t2 = evaluate(a.control, grid, 1.0, 0x7fu, pos);
        acc += t2.ticksUntilChange;
    }
    ChaosContext ctx;
    ctx.seed = 99;
    ctx.phraseTicks = phraseTicks;
    for (float chaos = 0.0f; chaos <= 1.0f; chaos += 0.05f)
    {
        ctx.chaos = chaos;
        ctx.sceneIndex = static_cast<std::uint32_t>(chaos * 20.0f) % 4u;
        sink = applyChaos(a, ctx);
        acc += sink.control.lane.count;
    }
    ctx.chaos = std::numeric_limits<float>::quiet_NaN();
    sink = applyChaos(a, ctx);
    for (float w = 0.0f; w <= 1.0f; w += 0.1f)
    {
        sink = morph(a, b, w, phraseTicks);
        acc += sink.control.lane.count;
        sink = blendDistance(a, b, w);
    }
    sink = morph(a, b, std::numeric_limits<float>::quiet_NaN(), phraseTicks);
    applyTrims(sink, trims);
    VariationSettings cand = b;
    acc += static_cast<double>(enforcePermissions(a, cand, false, phraseTicks));
    PhaseConfig phase;
    phase.policy = PhasePolicy::AlignBeat;
    phase.offsetBeats = 0.5f;
    for (double pos = -9.0; pos < 33.0; pos += 0.37)
        acc += resolvePhaseShift(phase, pos, grid) + resolveLocalPhase(phase, pos, grid);
    acc += resolvePhaseShift(phase, 1.0e300, grid) + resolvePhaseShift(phase, std::numeric_limits<double>::quiet_NaN(), grid);
    expectNoAlloc("PlacementEvaluator/Chaos/Euclid-chance/morph/trims/phase allocate nothing", region);
    expect("placement results finite", std::isfinite(acc));
}

void testChanceHash()
{
    EventLane lane;
    lane.chanceSeed = 5;
    LaneEvent e;
    e.chance = 50;
    std::size_t fired = 0;
    const NoAllocRegion region;
    for (std::int64_t cycle = -50000; cycle < 50000; ++cycle)
        fired += eventFires(lane, e, cycle) ? 1u : 0u;
    expectNoAlloc("eventFires allocates nothing", region);
    expect("eventFires ~50 percent", fired > 45000 && fired < 55000);
}

void testCrossover()
{
    constexpr int block = 192;
    dsp::BandSplit split;
    split.prepare(44100.0, block);
    std::vector<float> l(block), r(block);
    float* ch[2] = {l.data(), r.data()};
    Rng rng;
    bool finite = true;
    const NoAllocRegion region;
    for (int pass = 0; pass < 4; ++pass)
    {
        split.setTopology(pass % 2 == 0 ? dsp::Topology::LR4 : dsp::Topology::LR2);
        split.setMode(1 + pass / 2);
        for (int b = 0; b < 60; ++b)
        {
            split.setCutoff(b == 10 ? std::numeric_limits<double>::quiet_NaN() : 20.0 + 15.0 * b);
            for (int i = 0; i < block; ++i)
            {
                l[static_cast<std::size_t>(i)] = (b == 5 && i == 3) ? std::numeric_limits<float>::infinity() : rng.noise();
                r[static_cast<std::size_t>(i)] = (b == 6 && i == 9) ? std::numeric_limits<float>::quiet_NaN() : rng.noise();
            }
            split.split(ch, 2, block);
            split.merge(ch, 2, block);
            for (int i = 0; i < block; ++i)
                finite = finite && std::isfinite(l[static_cast<std::size_t>(i)]) && std::isfinite(r[static_cast<std::size_t>(i)]);
        }
    }
    expectNoAlloc("BandSplit split/merge allocate nothing", region);
    expect("BandSplit output finite for non-finite input", finite);
}

void testCapturesAndSources()
{
    constexpr int block = 480;
    constexpr double rate = 48000.0;
    std::vector<float> l(block), r(block);
    float* ch[2] = {l.data(), r.data()};
    const float* cch[2] = {l.data(), r.data()};
    Rng rng;
    for (int i = 0; i < block; ++i)
    {
        l[static_cast<std::size_t>(i)] = rng.noise();
        r[static_cast<std::size_t>(i)] = rng.noise();
    }

    OutputCapture out;
    out.prepare(rate);
    LoopSource source;
    source.prepare(rate);
    LoopCapture capture;
    capture.prepare(rate);
    std::vector<float> loopData(static_cast<std::size_t>(rate));
    for (auto& v : loopData)
        v = rng.noise();
    const float* loopCh[1] = {loopData.data()};
    expect("loop load", source.load(loopCh, 1, static_cast<std::int64_t>(loopData.size()), 4.0, 0.0));
    expect("output arm", out.arm(2, 4, 1.0));
    expect("capture arm", capture.arm(1, 4));
    OnsetDetector onset;
    onset.prepare(rate);
    std::vector<float> scratch(block);

    constexpr double spb = 24000.0; // 120 bpm at 48 kHz
    const NoAllocRegion region;
    double beat = 0.0;
    for (int b = 0; b < 400; ++b)
    {
        source.render(ch, 2, block, beat, spb);
        DemoSource::render(ch, 2, block, beat, spb, rate, 4);
        capture.process(cch, 2, block, beat, spb, true, 4);
        out.process(cch, 2, block, beat, spb, true);
        if (b == 150)
            out.abortBody(OutputCapture::Failure::Jumped); // later blocks keep running on an aborted state
        onset.process(l.data(), block, 0.7f);
        std::copy(l.begin(), l.end(), scratch.begin());
        extractWet(scratch.data(), r.data(), 0.5f, block);
        dsp::sanitizeBlock(ch, 2, block);
        // hostile arguments
        out.process(cch, 2, block, std::numeric_limits<double>::quiet_NaN(), spb, true);
        out.process(cch, 2, block, beat, -1.0, true);
        capture.process(cch, 2, block, 1.0e300, spb, true, 4);
        source.render(ch, 2, block, std::numeric_limits<double>::infinity(), spb);
        source.render(ch, 2, block, 1.0e300, 1.0e-300);
        beat += block / spb;
    }
    expectNoAlloc("OutputCapture/LoopSource/LoopCapture/Onset/Demo/sanitize allocate nothing", region);
    expect("capture finished or aborted by now", out.state() == OutputCapture::State::Aborted || out.state() == OutputCapture::State::Done);
}

void testSchedulerAndTransport()
{
    Scheduler scheduler;
    HostTransport host;
    PhraseGrid grid;
    scene::MidiNoteMap map;
    scene::MidiTriggerState trigger;
    AutoReturn autoReturn;
    LaunchPhase launch;
    CcInbox inbox;
    double acc = 0.0;
    const NoAllocRegion region;
    for (int i = 0; i < 20000; ++i)
    {
        host.tempoBpm = (i % 7 == 0) ? std::numeric_limits<double>::quiet_NaN() : 40.0 + (i % 400);
        host.meterNumerator = (i % 11) - 2;
        host.meterDenominator = (i % 13) - 3;
        host.isPlaying = (i % 3) != 0;
        host.timeInSamples = static_cast<std::int64_t>(i) * 512 * (i % 5 == 0 ? 1000000007ll : 1);
        host.ppqPosition = (i % 17 == 0) ? std::numeric_limits<double>::infinity() : i * 0.5;
        host.ppqPositionOfLastBarStart = (i % 19 == 0) ? -1.0e300 : i * 0.5 - 1.0;
        const auto t = TransportResolver::resolve(host, grid, i * 256ll, 48000.0, static_cast<TimingMode>(i % 3));
        if (t)
        {
            acc += t->positionInBeats() * 0.0;
            scheduler.request(static_cast<std::size_t>(i % 5), static_cast<TransitionMode>(i % 5));
            const auto ev = scheduler.process(*t, 256);
            acc += static_cast<double>(ev.sampleOffset);
            autoReturn.arm(t->positionInBars(), 1 + i % 4, static_cast<std::size_t>(i % 4));
            acc += autoReturn.poll(t->positionInBars()).value_or(0);
            acc += static_cast<double>(autoReturn.dryBoundaryOffset(t->positionInBars(), t->samplesPerBar()).value_or(0));
        }
        (void) trigger.noteOn(map, i % 128, static_cast<std::size_t>(i % 4));
        (void) trigger.noteOff(map, i % 128);
        (void) trigger.releaseAll(map);
        inbox.push(i % 140 - 5, i);
        PhaseConfig cfg;
        cfg.offsetBeats = static_cast<float>(i % 9) - 4.0f;
        launch.capture(static_cast<std::size_t>(i % 4), cfg);
        (void) launch.take(static_cast<std::size_t>(i % 4), cfg);
        (void) PhraseClock::samplesUntilBoundary(i * 777ll, 48000.0, grid, Quantization::Phrase);
        (void) PhraseClock::samplesUntilPpqBoundary(i * 0.25, std::nullopt, 48000.0, grid, Quantization::Bar);
    }
    expectNoAlloc("TransportResolver/Scheduler/AutoReturn/MidiTrigger/CcInbox/PhraseClock allocate nothing", region);
    expect("scheduler acc finite", std::isfinite(acc));
}

// The scene hand-off copies a SceneSnapshot on the audio thread (PluginProcessor::pullScene). It must stay a plain
// memcpy-able value of bounded size: no strings/vectors (heap), no pointers into message-thread data.
void testSnapshotBudget()
{
    static_assert(std::is_trivially_copyable_v<scene::SceneSnapshot>, "audio-thread scene copy must be a memcpy");
    static_assert(std::is_trivially_copyable_v<VariationSettings>, "scene settings must be trivially copyable");
    static_assert(std::is_trivially_destructible_v<scene::SceneSnapshot>, "scene copy must not own heap memory");
    // 4 scenes x (7 operations + 256-event lane + placement) is about 25 KiB today. A growth past 64 KiB would make the
    // per-publish copy (and cache footprint) worth a redesign: publish by pointer swap of preallocated slots.
    std::cout << "sizeof(LaneEvent)=" << sizeof(LaneEvent) << " sizeof(EventLane)=" << sizeof(EventLane)
              << " sizeof(VariationSettings)=" << sizeof(VariationSettings)
              << " sizeof(SceneSnapshot)=" << sizeof(scene::SceneSnapshot) << "\n";
    expect("SceneSnapshot stays under 64 KiB", sizeof(scene::SceneSnapshot) <= 64 * 1024);
    static scene::SceneSnapshot a, b;
    b.seed = 7;
    const NoAllocRegion region;
    for (int i = 0; i < 1000; ++i)
    {
        a = b;
        b.seed += static_cast<std::uint64_t>(a.activeIndex) + 1;
    }
    expectNoAlloc("SceneSnapshot copy allocates nothing", region);
}

// The counter itself must work: a deliberate allocation is seen.
void testHookWorks()
{
    const NoAllocRegion region;
    // volatile: the optimiser must not elide the new/delete pair (Apple clang does), or nothing is counted.
    int* volatile p = new int(3);
    const auto seen = region.allocations();
    delete p;
    expect("hook counts a deliberate allocation", seen == 1 && region.deallocations() == 1);
}
} // namespace

int main()
{
    testHookWorks();
    testSnapshotBudget();
    testEngine();
    testPlacementChaosMorph();
    testChanceHash();
    testCrossover();
    testCapturesAndSources();
    testSchedulerAndTransport();
    if (failures == 0)
        std::cout << "RtAllocationTests passed\n";
    return failures == 0 ? 0 : 1;
}
