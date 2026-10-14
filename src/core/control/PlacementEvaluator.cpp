#include "core/control/PlacementEvaluator.h"

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
           || (c.activity.enabled && c.activity.maxConcurrent < laneCount);
}

Targets evaluate(const SceneControl& c, const GridContext& grid, double positionBeats,
                 std::uint32_t enabledMask) noexcept
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
    const double pos = positionBeats * ticksPerBeat;
    const double q = pos - static_cast<double>(c.placement.originTicks);
    const double phraseIndex = std::floor(q / P);
    const double phase = positiveMod(q, P);
    double until = distanceTo(phase, 0.0, P); // phrase boundary always limits the span

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
    on.fill(windowOpen);
    scale.fill(1.0f);
    if (c.lane.explicitEvents)
    {
        on.fill(false);
        for (std::size_t i = 0; i < c.lane.count && i < maxLaneEvents; ++i)
        {
            const auto& e = c.lane.events[i];
            if (!e.enabled || e.operation >= laneCount)
                continue;
            const double onset = static_cast<double>(e.onsetTicks);
            const double dur = static_cast<double>(e.durationTicks);
            until = std::min(until, distanceTo(phase, onset, P));
            until = std::min(until, distanceTo(phase, onset + dur, P));
            if (windowOpen && !on[e.operation] && positiveMod(phase - onset, P) < dur)
            {
                on[e.operation] = true;
                scale[e.operation] = std::clamp(e.depth, 0.0f, 1.0f);
            }
        }
    }

    if (c.placement.downbeatProtectBeats > 0.0f && barTicks > 0.0)
    {
        const double protect = std::min(barTicks,
            static_cast<double>(c.placement.downbeatProtectBeats) * ticksPerBeat);
        const double barPhase = positiveMod(pos, barTicks);
        if (barPhase < protect)
            on.fill(false);
        until = std::min(until, distanceTo(barPhase, protect, barTicks));
        until = std::min(until, distanceTo(barPhase, 0.0, barTicks));
    }

    if (c.activity.enabled && c.activity.maxConcurrent < laneCount)
    {
        std::array<std::size_t, laneCount> order{};
        std::size_t n = 0;
        for (std::size_t op = 0; op < laneCount; ++op)
            if (on[op] && (enabledMask & (1u << op)) != 0)
                order[n++] = op;
        std::stable_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(n),
                         [&c](std::size_t a, std::size_t b) {
                             return c.activity.priority[a] > c.activity.priority[b];
                         });
        for (std::size_t k = c.activity.maxConcurrent; k < n; ++k)
            on[order[k]] = false;
    }

    for (std::size_t op = 0; op < laneCount; ++op)
    {
        t.active[op] = on[op];
        t.depthScale[op] = scale[op];
    }
    t.ticksUntilChange = until;
    return t;
}
} // namespace groovemorph::core::control
