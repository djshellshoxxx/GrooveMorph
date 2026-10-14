#include "core/control/PhaseResolver.h"
#include "core/control/PlacementEvaluator.h"
#include "core/control/VariationBlend.h"
#include "core/scene/SceneBank.h"
#include "core/scene/SceneSerializer.h"

#include <cmath>
#include <iostream>
#include <limits>
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
bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
} // namespace

int main()
{
    GridContext grid44;
    grid44.beatsPerBar = 4;
    grid44.phraseBars = 4; // 16 meter beats
    GridContext grid78;
    grid78.beatsPerBar = 7;
    grid78.phraseBars = 2; // 14 meter beats

    // C04-T01 (meter-beat units): switch at global beat 6.25.
    {
        PhaseConfig preserve, restart, align;
        restart.policy = PhasePolicy::Restart;
        align.policy = PhasePolicy::AlignBeat;
        expect("preserve", near(resolveLocalPhase(preserve, 6.25, grid44), 6.25));
        expect("restart", near(resolveLocalPhase(restart, 6.25, grid44), 0.0));
        expect("align", near(resolveLocalPhase(align, 6.25, grid44), 7.0));
        preserve.offsetBeats = restart.offsetBeats = align.offsetBeats = -0.25f;
        expect("preserve offset", near(resolveLocalPhase(preserve, 6.25, grid44), 6.0));
        expect("restart offset wraps", near(resolveLocalPhase(restart, 6.25, grid44), 15.75));
        expect("align offset", near(resolveLocalPhase(align, 6.25, grid44), 6.75));
        align.offsetBeats = 0.0f;
        expect("align at an exact beat holds the beat", near(resolveLocalPhase(align, 7.0, grid44), 7.0));
        expect("shift identity for Preserve/0", resolvePhaseShift(PhaseConfig{}, 12345.678, grid44) == 0.0);
        const double shift = resolvePhaseShift(restart, 6.25, grid44);
        expect("shift reproduces local phase", near(std::fmod(6.25 + shift, 16.0), 15.75));
    }
    // C04-T02: 7/8 uses the meter beat, not a quarter note.
    {
        PhaseConfig align;
        align.policy = PhasePolicy::AlignBeat;
        expect("meter-beat snap", near(resolveLocalPhase(align, 3.3, grid78), 4.0));
    }
    // Non-finite and out-of-range configuration is recovered.
    {
        PhaseConfig bad;
        bad.offsetBeats = std::numeric_limits<float>::quiet_NaN();
        bad.policy = static_cast<PhasePolicy>(9);
        expect("sanitize reports a change", sanitize(bad));
        expect("sanitized values", bad.policy == PhasePolicy::Preserve && bad.offsetBeats == 0.0f);
        bad.offsetBeats = 99.0f;
        sanitize(bad);
        expect("offset clamped", bad.offsetBeats == 4.0f);
        PhaseConfig nan;
        expect("non-finite position", std::isfinite(resolveLocalPhase(nan, std::nan(""), grid44)));
    }
    // Protected regions mute every effect, the phase shift keeps cadence/downbeats on the global grid.
    {
        SceneControl c;
        c.placement.regionCount = 1;
        c.placement.regions[0] = {ticksPerBeat, ticksPerBeat}; // beat 1..2
        const auto inside = evaluate(c, grid44, 1.5);
        const auto outside = evaluate(c, grid44, 3.5);
        bool allOff = true, allOn = true;
        for (std::size_t i = 0; i < laneCount; ++i)
        {
            allOff = allOff && !inside.active[i];
            allOn = allOn && outside.active[i];
        }
        expect("region mutes", allOff);
        expect("outside the region is untouched", allOn);
        expect("region boundary limits the span", near(inside.ticksUntilChange, 0.5 * ticksPerBeat));

        SceneControl cadence;
        cadence.placement.enabled = true;
        cadence.placement.every = 2;
        const auto globalEven = evaluate(cadence, grid44, 3.0, 0x1f, 0.0);   // global phrase 0
        const auto globalOdd = evaluate(cadence, grid44, 3.0, 0x1f, 16.0);   // global phrase 1
        expect("cadence follows the global phrase", globalEven.active[0] && !globalOdd.active[0]);
    }
    // Per-event locks survive regeneration; phase/regions/locks round-trip through the scene format.
    {
        VariationSettings source;
        source.control.lane.explicitEvents = true;
        source.control.lane.count = 2;
        source.control.lane.events[0] = LaneEvent{0, true, 0, 480, 1.0f, 0.0f, 50, false, true};
        source.control.lane.events[1] = LaneEvent{1, true, 960, 480, 1.0f, 0.0f, 50, false, false};
        VariationSettings candidate = source;
        candidate.control.lane.count = 0;
        candidate.control.lane.events[0] = {};
        candidate.control.lane.events[1] = {};
        const auto restored = enforcePermissions(source, candidate);
        expect("a per-event lock restores the event", restored > 0 && candidate.control.lane.count == 1
                                                          && candidate.control.lane.events[0].locked
                                                          && candidate.control.lane.events[0].operation == 0);

        scene::SceneBank bank;
        auto v = bank.variation(1);
        v.settings = source;
        v.settings.control.phase = {PhasePolicy::AlignBeat, 0.5f};
        v.settings.control.placement.regionCount = 2;
        v.settings.control.placement.regions[0] = {100, 200};
        v.settings.control.placement.regions[1] = {1920, 480};
        bank.setVariation(1, v);
        const auto text = scene::SceneSerializer::serialize(bank);
        const auto loaded = scene::SceneSerializer::deserialize(text);
        const auto& back = loaded.bank.variation(1).settings.control;
        expect("phase, regions and locks round-trip",
               loaded.bank == bank && back.phase.policy == PhasePolicy::AlignBeat && back.phase.offsetBeats == 0.5f
                   && back.placement.regionCount == 2 && back.placement.regions[1].startTicks == 1920
                   && back.lane.events[0].locked && !back.lane.events[1].locked
                   && scene::SceneSerializer::serialize(loaded.bank) == text);
    }
    // Toggling the per-event lock is a lock-configuration edit: it never loses or duplicates the event.
    {
        VariationSettings stored;
        stored.control.lane.explicitEvents = true;
        stored.control.lane.count = 1;
        stored.control.lane.events[0] = LaneEvent{1, true, 0, 100, 1.0f, 0.0f, 50, false, false};
        VariationSettings lockIt = stored;
        lockIt.control.lane.events[0].locked = true;
        enforcePermissions(stored, lockIt, true);
        expect("locking an event keeps it, now locked", lockIt.control.lane.count == 1 && lockIt.control.lane.events[0].locked);

        VariationSettings storedLocked = lockIt;
        VariationSettings unlockIt = storedLocked;
        unlockIt.control.lane.events[0].locked = false;
        enforcePermissions(storedLocked, unlockIt, true);
        expect("unlocking leaves one unlocked event", unlockIt.control.lane.count == 1 && !unlockIt.control.lane.events[0].locked);

        VariationSettings moved = storedLocked;
        moved.control.lane.events[0].locked = false;
        moved.control.lane.events[0].onsetTicks = 50; // edited while locked
        const auto restored = enforcePermissions(storedLocked, moved, true);
        expect("an edit of a locked event is undone without duplicating it",
               restored > 0 && moved.control.lane.count == 1 && moved.control.lane.events[0].onsetTicks == 0
                   && moved.control.lane.events[0].locked);

        VariationSettings overlapAdd = storedLocked;
        overlapAdd.control.lane.count = 2;
        overlapAdd.control.lane.events[1] = LaneEvent{1, true, 50, 100, 1.0f, 0.0f, 50, false, false};
        enforcePermissions(storedLocked, overlapAdd, true, 960);
        expect("a new event over a locked one is dropped", overlapAdd.control.lane.count == 1
                                                               && overlapAdd.control.lane.events[0].onsetTicks == 0);
        VariationSettings elsewhere = storedLocked;
        elsewhere.control.lane.count = 2;
        elsewhere.control.lane.events[1] = LaneEvent{1, true, 500, 100, 1.0f, 0.0f, 50, false, false};
        enforcePermissions(storedLocked, elsewhere, true, 960);
        expect("a new event elsewhere is kept", elsewhere.control.lane.count == 2);
    }

    if (failures == 0)
        std::cout << "PhaseResolverTests passed\n";
    return failures == 0 ? 0 : 1;
}
