#include "core/control/PlacementEvaluator.h"
#include "core/control/Chance.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::control
{
namespace
{
constexpr double eps = 1.0e-7;

double positiveMod(double a, double m) noexcept
{
    const double r = std::fmod(a, m);
    return r < 0.0 ? r + m : r;
}

// Circular distance (ticks) from `phase` forward to boundary `b` in a ring of size `ring`.
double distanceTo(double phase, double b, double ring) noexcept
{
    double d = positiveMod(b, ring) - phase;
    if (d <= eps)
        d += ring;
    return d;
}
} // namespace

bool isActive(const SceneControl& c) noexcept
{
    return c.placement.enabled || c.lane.explicitEvents || c.placement.downbeatProtectBeats > 0.0f
           || c.placement.regionCount > 0
           || (c.activity.enabled && c.activity.maxConcurrent < laneCount);
}

Targets evaluate(const SceneControl& c, const GridContext& grid, double positionBeats,
                 std::uint32_t enabledMask, double globalBeats) noexcept
{
    Targets t;
    t.active.fill(true);
    t.depthScale.fill(1.0f);
    const std::int64_t phraseTicksInt = grid.phraseTicks();
    if (!isActive(c) || phraseTicksInt <= 0 || !std::isfinite(positionBeats))
        return t;
    t.gated = true;

    const double P = static_cast<double>(phraseTicksInt);
    const double barTicks = static_cast<double>(grid.beatsPerBar) * ticksPerBeat;
    // Every boundary is an integer tick; a position a hair short of one (floating-point beat sums)
    // must count as being on it, or a whole window would be missed.
    const auto snapToTick = [](double ticks) {
        const double nearest = std::round(ticks);
        return std::abs(ticks - nearest) < 1.0e-6 ? nearest : ticks;
    };
    const double pos = snapToTick(positionBeats * ticksPerBeat);
    const double q = pos - static_cast<double>(c.placement.originTicks);
    const bool hasGlobal = std::isfinite(globalBeats);
    const double globalPos = hasGlobal ? snapToTick(globalBeats * ticksPerBeat) : pos;
    const double globalQ = globalPos - static_cast<double>(c.placement.originTicks);
    const double phraseIndex = std::floor(globalQ / P);
    const double phase = positiveMod(q, P);
    double until = distanceTo(phase, 0.0, P); // phrase boundary always limits the span
    if (hasGlobal)
        until = std::min(until, distanceTo(positiveMod(globalQ, P), 0.0, P)); // cadence changes here

    bool windowOpen = true;
    if (c.placement.enabled)
    {
        const double every = static_cast<double>(std::max<std::uint8_t>(1, c.placement.every));
        const double wanted = static_cast<double>(c.placement.cadenceOffset % std::max<std::uint8_t>(1, c.placement.every));
        windowOpen = positiveMod(phraseIndex, every) == wanted;
        if (windowOpen && c.placement.window != WindowMode::FullPhrase)
        {
            double onset = 0.0, span = P;
            if (c.placement.window == WindowMode::Custom)
            {
                onset = std::fmod(std::round(static_cast<double>(c.placement.startPhase) * P), P);
                span = std::clamp(std::round(static_cast<double>(c.placement.spanPhase) * P), 1.0, P);
            }
            else if (barTicks < P)
            {
                onset = P - barTicks;
                span = barTicks;
            }
            if (span < P)
            {
                windowOpen = positiveMod(phase - onset, P) < span;
                until = std::min(until, distanceTo(phase, onset, P));
                until = std::min(until, distanceTo(phase, onset + span, P));
            }
        }
    }

    std::array<bool, laneCount> on{};
    std::array<float, laneCount> scale{};
    std::array<float, laneCount> slice{};
    std::array<double, laneCount> offset{};
    std::array<int, laneCount> eventPriority{};
    on.fill(windowOpen);
    scale.fill(1.0f);
    eventPriority.fill(-1);
    if (c.lane.explicitEvents)
    {
        on.fill(false);
        for (std::size_t i = 0; i < c.lane.count && i < maxLaneEvents; ++i)
        {
            const auto& e = c.lane.events[i];
            if (!e.enabled || e.operation >= laneCount)
                continue;
            // F06: an operation may loop its events on its own length instead of the phrase.
            const std::uint8_t loop = c.lane.loopBeats[e.operation];
            const double ring = loop > 0 ? static_cast<double>(loop) * ticksPerBeat : P;
            const double opPhase = loop > 0 ? positiveMod(q, ring) : phase;
            const double onset = std::fmod(static_cast<double>(e.onsetTicks), ring);
            const double dur = std::min(static_cast<double>(e.durationTicks), ring);
            until = std::min(until, distanceTo(opPhase, onset, ring));
            until = std::min(until, distanceTo(opPhase, onset + dur, ring));
            if (loop > 0)
                until = std::min(until, distanceTo(opPhase, 0.0, ring));
            const double since = positiveMod(opPhase - onset, ring);
            if (windowOpen && !on[e.operation] && since < dur)
            {
                // N03 chance: one pure decision per lane cycle (the cycle that holds the event onset).
                if ((e.chance < 100 || c.lane.chanceByOp[e.operation] < 100)
                    && !eventFires(c.lane, e, static_cast<std::int64_t>(std::floor((q - onset) / ring))))
                    continue;
                on[e.operation] = true;
                scale[e.operation] = std::clamp(e.depth, 0.0f, 1.0f);
                slice[e.operation] = e.sliceBeats;
                eventPriority[e.operation] = e.priority;
                if (e.restartCycle)
                    offset[e.operation] = (pos - since) / ticksPerBeat;
            }
        }
    }

    if (c.placement.downbeatProtectBeats > 0.0f && barTicks > 0.0)
    {
        const double protect = std::min(barTicks,
            static_cast<double>(c.placement.downbeatProtectBeats) * ticksPerBeat);
        const double barPhase = positiveMod(globalPos, barTicks); // bar starts are audible downbeats
        if (barPhase < protect)
            on.fill(false);
        until = std::min(until, distanceTo(barPhase, protect, barTicks));
        until = std::min(until, distanceTo(barPhase, 0.0, barTicks));
    }

    for (std::size_t i = 0; i < c.placement.regionCount && i < maxProtectedRegions; ++i)
    {
        const auto& r = c.placement.regions[i];
        if (r.lengthTicks < 1)
            continue;
        const double length = std::min(static_cast<double>(r.lengthTicks), P);
        const double start = std::fmod(static_cast<double>(r.startTicks), P);
        if (positiveMod(phase - start, P) < length)
            on.fill(false);
        until = std::min(until, distanceTo(phase, start, P));
        until = std::min(until, distanceTo(phase, start + length, P));
    }

    if (c.activity.enabled && c.activity.maxConcurrent < laneCount)
    {
        std::array<std::size_t, laneCount> order{};
        std::size_t n = 0;
        for (std::size_t op = 0; op < laneCount; ++op)
            if (on[op] && (enabledMask & (1u << op)) != 0)
                order[n++] = op;
        // Stable insertion sort (n <= 5): std::stable_sort may allocate a temporary buffer.
        const auto priorityOf = [&c, &eventPriority](std::size_t op) {
            return eventPriority[op] >= 0 ? eventPriority[op] : static_cast<int>(c.activity.priority[op]);
        };
        for (std::size_t i = 1; i < n; ++i)
        {
            const std::size_t key = order[i];
            std::size_t j = i;
            while (j > 0 && priorityOf(order[j - 1]) < priorityOf(key))
            {
                order[j] = order[j - 1];
                --j;
            }
            order[j] = key;
        }
        for (std::size_t k = c.activity.maxConcurrent; k < n; ++k)
            on[order[k]] = false;
    }

    for (std::size_t op = 0; op < laneCount; ++op)
    {
        t.active[op] = on[op];
        t.depthScale[op] = scale[op];
        t.sliceBeats[op] = slice[op];
        t.phaseOffsetBeats[op] = offset[op];
    }
    t.ticksUntilChange = until;
    return t;
}
} // namespace groovemorph::core::control
