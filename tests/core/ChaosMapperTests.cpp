// N05 acceptance tests C1-C6 (docs/specs/N05-EASY-MODE.md).
#include "core/control/ChaosMapper.h"
#include "core/control/VariationBlend.h"
#include "core/scene/SceneBank.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>
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

constexpr std::int64_t phraseTicks = 4 * 4 * ticksPerBeat;

// Quiet scene: only Gate is on, shallow and long; everything else off.
VariationSettings quietScene()
{
    VariationSettings s;
    for (auto& op : s.operations)
        op = {false, 0.3f, 1.0f};
    s[OperationType::Gate] = {true, 0.4f, 1.0f};
    return s;
}

VariationSettings explicitScene()
{
    auto s = quietScene();
    s.control.lane.explicitEvents = true;
    LaneEvent e;
    e.operation = static_cast<std::uint8_t>(OperationType::Gate);
    e.onsetTicks = 0;
    e.durationTicks = ticksPerBeat;
    s.control.lane.events[0] = e;
    s.control.lane.count = 1;
    return s;
}

struct Activity
{
    int enabled = 0;
    double depth = 0.0;
    double inverseSlice = 0.0;
    int events = 0;
};

Activity measure(const VariationSettings& s)
{
    Activity a;
    for (const auto& op : s.operations)
        if (op.enabled)
        {
            ++a.enabled;
            a.depth += op.depth;
            a.inverseSlice += 1.0 / op.sliceBeats;
        }
    a.events = s.control.lane.count;
    return a;
}

ChaosContext context(float chaos, std::uint64_t seed = 7, std::uint32_t scene = 0)
{
    ChaosContext c;
    c.chaos = chaos;
    c.seed = seed;
    c.sceneIndex = scene;
    c.phraseTicks = phraseTicks;
    return c;
}

bool identical(const VariationSettings& a, const VariationSettings& b)
{
    static_assert(std::is_trivially_copyable_v<VariationSettings>);
    return std::memcmp(&a, &b, sizeof(VariationSettings)) == 0 || scene::sameSettings(a, b);
}

bool laneValid(const EventLane& lane)
{
    if (lane.count > maxLaneEvents)
        return false;
    for (std::size_t i = 0; i < lane.count; ++i)
    {
        const auto& a = lane.events[i];
        if (a.onsetTicks < 0 || a.durationTicks < 1 || a.onsetTicks + a.durationTicks > phraseTicks + 0)
            return false;
        for (std::size_t j = i + 1; j < lane.count; ++j)
            if (lane.events[j].operation == a.operation && eventsOverlap(a, lane.events[j], phraseTicks))
                return false;
    }
    return true;
}
} // namespace

int main()
{
    const auto quiet = quietScene();
    const auto withEvents = explicitScene();

    // N05-C2: chaos 0, NaN and negative leave the scene bit-identical; input never mutated.
    {
        for (const auto& source : {quiet, withEvents})
        {
            const auto before = source;
            for (const float c : {0.0f, -0.5f, -0.0f, std::numeric_limits<float>::quiet_NaN(),
                                  -std::numeric_limits<float>::infinity()})
            {
                const auto out = applyChaos(source, context(c));
                expect("chaos <= 0 or NaN is bit-identical", std::memcmp(&out, &source, sizeof(out)) == 0);
            }
            expect("input is never mutated", std::memcmp(&before, &source, sizeof(source)) == 0);
            const auto busy = applyChaos(source, context(1.0f));
            (void) busy;
            expect("input untouched after a full chaos call", std::memcmp(&before, &source, sizeof(source)) == 0);
        }
        // chaos above 1 behaves exactly like 1.
        expect("chaos > 1 clamps to 1", identical(applyChaos(quiet, context(5.0f)), applyChaos(quiet, context(1.0f))));
    }

    // N05-C1: determinism, and sensitivity to seed and scene index.
    {
        for (const auto& source : {quiet, withEvents})
        {
            const auto a = applyChaos(source, context(0.6f));
            const auto b = applyChaos(source, context(0.6f));
            expect("same inputs same output", std::memcmp(&a, &b, sizeof(a)) == 0);
        }
        bool seedDiffers = false, sceneDiffers = false;
        const auto base = applyChaos(quiet, context(0.5f, 7, 0));
        for (std::uint64_t seed = 8; seed < 40 && !seedDiffers; ++seed)
            seedDiffers = !scene::sameSettings(base, applyChaos(quiet, context(0.5f, seed, 0)));
        for (std::uint32_t index = 1; index < 4 && !sceneDiffers; ++index)
            sceneDiffers = !scene::sameSettings(base, applyChaos(quiet, context(0.5f, 7, index)));
        expect("seed changes the result", seedDiffers);
        expect("scene index changes the result", sceneDiffers);
    }

    // N05-C3: activity is monotone in chaos over a fine sweep, for many seeds and both scene shapes.
    {
        bool monotone = true, grows = false, eventsGrow = false;
        for (const auto& source : {quiet, withEvents})
            for (std::uint64_t seed = 1; seed <= 24; ++seed)
                for (std::uint32_t index = 0; index < 4; ++index)
                {
                    Activity previous = measure(applyChaos(source, context(0.0f, seed, index)));
                    for (int step = 1; step <= 100; ++step)
                    {
                        const auto now = measure(applyChaos(source, context(static_cast<float>(step) / 100.0f, seed, index)));
                        monotone = monotone && now.enabled >= previous.enabled && now.events >= previous.events
                                   && now.depth >= previous.depth - 1e-5 && now.inverseSlice >= previous.inverseSlice - 1e-4;
                        previous = now;
                    }
                    const auto start = measure(source);
                    grows = grows || (previous.enabled > start.enabled && previous.depth > start.depth
                                      && previous.inverseSlice > start.inverseSlice);
                    eventsGrow = eventsGrow || previous.events > start.events;
                }
        expect("activity never decreases as chaos rises", monotone);
        expect("full chaos is busier than none", grows);
        expect("explicit lanes gain events", eventsGrow);
    }

    // N05-C6: chaos 1 enables every unlocked operation and never lengthens a slice.
    {
        for (std::uint64_t seed = 1; seed <= 16; ++seed)
        {
            const auto out = applyChaos(quiet, context(1.0f, seed, 1));
            bool allOn = true, noLonger = true;
            for (std::size_t i = 0; i < operationCount; ++i)
            {
                allOn = allOn && out.operations[i].enabled;
                noLonger = noLonger && out.operations[i].sliceBeats <= quiet.operations[i].sliceBeats;
            }
            expect("chaos 1 enables everything unlocked", allOn);
            expect("slices only get shorter", noLonger);
        }
    }

    // N05-C5: outputs stay valid and stored events are kept.
    {
        bool valid = true, kept = true, sane = true;
        for (std::uint64_t seed = 1; seed <= 30; ++seed)
            for (int step = 0; step <= 10; ++step)
            {
                const auto out = applyChaos(withEvents, context(static_cast<float>(step) / 10.0f, seed, seed % 4));
                valid = valid && laneValid(out.control.lane);
                kept = kept && out.control.lane.count >= 1 && std::memcmp(&out.control.lane.events[0], &withEvents.control.lane.events[0],
                                                                         sizeof(LaneEvent)) == 0;
                auto copy = out;
                sane = sane && !scene::sanitize(copy);
                for (const auto& op : out.operations)
                    sane = sane && op.depth >= 0.0f && op.depth <= 1.0f && op.sliceBeats >= 1.0f / 16.0f && op.sliceBeats <= 4.0f;
            }
        expect("lane stays valid (capacity, no overlap)", valid);
        expect("stored events are kept unchanged", kept);
        expect("output is already sanitized and in range", sane);
        // Lane at capacity cannot grow.
        auto full = withEvents;
        for (std::size_t i = 1; i < maxLaneEvents; ++i)
        {
            LaneEvent e;
            e.operation = static_cast<std::uint8_t>(OperationType::Repeat);
            e.onsetTicks = static_cast<std::int32_t>((i * 7) % 15) * ticksPerBeat + 0;
            e.durationTicks = ticksPerBeat / 8;
            full.control.lane.events[i] = e;
        }
        full.control.lane.count = maxLaneEvents;
        expect("a full lane stays at capacity", applyChaos(full, context(1.0f)).control.lane.count == maxLaneEvents);
    }

    // N05-C4: locks, placement lock, protected regions, never-touch.
    {
        for (std::uint64_t seed = 1; seed <= 20; ++seed)
        {
            auto source = withEvents;
            source.operations[0] = {false, 0.2f, 2.0f};   // Repeat: off, enable-locked
            source.operations[1] = {true, 0.3f, 1.0f};    // Reverse: depth-locked
            source.operations[2] = {true, 0.4f, 1.0f};    // Gate: slice-locked
            source.operations[3] = {false, 0.5f, 1.0f};   // Half-time: events-locked (still enable-able)
            source.operations[4] = {false, 0.1f, 1.0f};   // Tape stop: never-touch
            source.control.permissions.operationLocks[0] = lock::enable;
            source.control.permissions.operationLocks[1] = lock::depth;
            source.control.permissions.operationLocks[2] = lock::slice;
            source.control.permissions.operationLocks[3] = lock::events;
            ChaosContext c = context(1.0f, seed, 2);
            c.neverTouchMask = 1u << 4;
            const auto out = applyChaos(source, c);
            expect("enable lock keeps the operation off", !out.operations[0].enabled && out.operations[0].depth == source.operations[0].depth
                                                            && out.operations[0].sliceBeats == source.operations[0].sliceBeats);
            expect("depth lock keeps depth", out.operations[1].depth == source.operations[1].depth);
            expect("slice lock keeps slice", out.operations[2].sliceBeats == source.operations[2].sliceBeats);
            expect("never-touch operation is unchanged", std::memcmp(&out.operations[4], &source.operations[4], sizeof(OperationSettings)) == 0);
            bool noEventsOnLocked = true;
            for (std::size_t i = 0; i < out.control.lane.count; ++i)
            {
                const auto op = out.control.lane.events[i].operation;
                noEventsOnLocked = noEventsOnLocked && op != 3 && op != 4 && op != 0;
            }
            expect("no events on events-locked, never-touch or disabled-locked operations", noEventsOnLocked);
            expect("permissions untouched", out.control.permissions.operationLocks[2] == lock::slice);
            auto check = out;
            expect("enforcePermissions restores nothing", enforcePermissions(source, check) == 0);
        }

        // Per-event lock: the locked event is kept exactly and nothing is added over it.
        auto lockedScene = withEvents;
        lockedScene.control.lane.events[0].locked = true;
        for (std::uint64_t seed = 1; seed <= 20; ++seed)
        {
            const auto out = applyChaos(lockedScene, context(1.0f, seed));
            expect("locked event unchanged", std::memcmp(&out.control.lane.events[0], &lockedScene.control.lane.events[0], sizeof(LaneEvent)) == 0);
            auto check = out;
            expect("enforcePermissions restores nothing (event lock)", enforcePermissions(lockedScene, check) == 0);
        }

        // Placement lock: no events added.
        auto placementLocked = withEvents;
        placementLocked.control.permissions.placementLocked = true;
        bool noneAdded = true;
        for (std::uint64_t seed = 1; seed <= 20; ++seed)
            noneAdded = noneAdded && applyChaos(placementLocked, context(1.0f, seed)).control.lane.count == 1;
        expect("placement lock stops event additions", noneAdded);

        // Protected region covers the whole phrase except the first beat: nothing may land inside it.
        auto regioned = withEvents;
        regioned.control.placement.regionCount = 1;
        regioned.control.placement.regions[0] = {ticksPerBeat, static_cast<std::int32_t>(phraseTicks - ticksPerBeat)};
        bool respected = true;
        for (std::uint64_t seed = 1; seed <= 40; ++seed)
        {
            const auto out = applyChaos(regioned, context(1.0f, seed));
            for (std::size_t i = 1; i < out.control.lane.count; ++i)
            {
                const auto& e = out.control.lane.events[i];
                respected = respected && e.onsetTicks >= 0 && e.onsetTicks + e.durationTicks <= ticksPerBeat;
            }
        }
        expect("added events avoid protected regions", respected);

        // Legacy-uniform lanes get no events at all (density comes from depth and slice).
        expect("legacy lane stays without events", applyChaos(quiet, context(1.0f)).control.lane.count == 0
                                                       && !applyChaos(quiet, context(1.0f)).control.lane.explicitEvents);

        // Fully locked scene: every field frozen.
        auto frozen = quiet;
        for (auto& l : frozen.control.permissions.operationLocks)
            l = lock::all;
        const auto out = applyChaos(frozen, context(1.0f));
        expect("a fully locked scene does not change", scene::sameSettings(out, frozen));
    }

    if (failures == 0)
        std::cout << "ChaosMapperTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
