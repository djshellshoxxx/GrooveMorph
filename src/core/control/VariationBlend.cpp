#include "core/control/VariationBlend.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::control
{
namespace
{
float clamp01(float v) noexcept { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }

float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

float logLerp(float a, float b, float t) noexcept
{
    if (!(a > 0.0f) || !(b > 0.0f))
        return lerp(a, b, t);
    return std::exp2(lerp(std::log2(a), std::log2(b), t));
}
} // namespace

VariationSettings blendDistance(const VariationSettings& source, const VariationSettings& candidate,
                                float distance) noexcept
{
    const float t = clamp01(distance);
    VariationSettings out = source;
    for (std::size_t i = 0; i < operationCount; ++i)
    {
        const auto& s = source.operations[i];
        const auto& c = candidate.operations[i];
        auto& o = out.operations[i];
        o.depth = std::clamp(lerp(s.depth, c.depth, t), 0.0f, 1.0f);
        o.sliceBeats = std::clamp(logLerp(s.sliceBeats, c.sliceBeats, t), 1.0f / 16.0f, 4.0f);
        o.enabled = (t >= 0.5f) ? c.enabled : s.enabled;
        if (t >= 1.0f)
            o = c;
        if (t <= 0.0f)
            o = s;
    }
    return out;
}

VariationSettings morph(const VariationSettings& a, const VariationSettings& b, float weight) noexcept
{
    const float w = clamp01(weight);
    VariationSettings out = a;
    for (std::size_t i = 0; i < operationCount; ++i)
    {
        const auto& x = a.operations[i];
        const auto& y = b.operations[i];
        auto& o = out.operations[i];
        if (x.enabled && y.enabled)
        {
            o.enabled = true;
            o.depth = lerp(x.depth, y.depth, w);
            o.sliceBeats = logLerp(x.sliceBeats, y.sliceBeats, w);
        }
        else if (x.enabled)
        {
            o = x;
            o.depth = x.depth * (1.0f - w);
            o.enabled = o.depth > 0.0f;
        }
        else if (y.enabled)
        {
            o = y;
            o.depth = y.depth * w;
            o.enabled = o.depth > 0.0f;
        }
        else
        {
            o = x;
        }
    }
    return out;
}

std::uint32_t enforcePermissions(const VariationSettings& source, VariationSettings& candidate,
                                 bool keepCandidatePermissions) noexcept
{
    std::uint32_t restored = 0;
    const auto& perms = source.control.permissions;
    for (std::size_t i = 0; i < operationCount; ++i)
    {
        const auto locks = perms.operationLocks[i];
        auto& c = candidate.operations[i];
        const auto& s = source.operations[i];
        if ((locks & lock::enable) != 0 && c.enabled != s.enabled) { c.enabled = s.enabled; ++restored; }
        if ((locks & lock::depth) != 0 && c.depth != s.depth) { c.depth = s.depth; ++restored; }
        if ((locks & lock::slice) != 0 && c.sliceBeats != s.sliceBeats) { c.sliceBeats = s.sliceBeats; ++restored; }
    }
    // Events lock: lane events of a locked operation are restored from the source.
    bool eventsLocked = false;
    for (auto l : perms.operationLocks)
        eventsLocked = eventsLocked || (l & lock::events) != 0;
    // Only touch the lane when a locked operation's events actually differ (order-insensitive), so
    // unrelated edits neither reorder events nor report a restore.
    bool lockedEventsDiffer = false;
    if (eventsLocked)
    {
        const auto collect = [&perms](const EventLane& lane, std::array<LaneEvent, maxLaneEvents>& out) {
            std::size_t n = 0;
            for (std::size_t k = 0; k < lane.count && k < maxLaneEvents; ++k)
                if ((perms.operationLocks[lane.events[k].operation % laneCount] & lock::events) != 0)
                    out[n++] = lane.events[k];
            return n;
        };
        std::array<LaneEvent, maxLaneEvents> a{}, b{};
        const std::size_t na = collect(source.control.lane, a), nb = collect(candidate.control.lane, b);
        lockedEventsDiffer = na != nb;
        for (std::size_t i = 0; i < na && !lockedEventsDiffer; ++i)
        {
            bool found = false;
            for (std::size_t j = 0; j < nb && !found; ++j)
                found = a[i].operation == b[j].operation && a[i].onsetTicks == b[j].onsetTicks
                        && a[i].durationTicks == b[j].durationTicks && a[i].depth == b[j].depth
                        && a[i].sliceBeats == b[j].sliceBeats && a[i].priority == b[j].priority
                        && a[i].enabled == b[j].enabled && a[i].restartCycle == b[j].restartCycle;
            lockedEventsDiffer = !found;
        }
    }
    if (eventsLocked && lockedEventsDiffer)
    {
        EventLane merged;
        merged.loopBeats = candidate.control.lane.loopBeats;
        merged.explicitEvents = source.control.lane.explicitEvents || candidate.control.lane.explicitEvents;
        const auto locked = [&perms](const LaneEvent& e) {
            return (perms.operationLocks[e.operation % laneCount] & lock::events) != 0;
        };
        const auto push = [&merged](const LaneEvent& e) {
            if (merged.count < maxLaneEvents)
                merged.events[merged.count++] = e;
        };
        // Locked events are mandatory, so they are placed first and can never be crowded out.
        for (std::size_t k = 0; k < source.control.lane.count && k < maxLaneEvents; ++k)
            if (locked(source.control.lane.events[k]))
                push(source.control.lane.events[k]);
        for (std::size_t k = 0; k < candidate.control.lane.count && k < maxLaneEvents; ++k)
            if (!locked(candidate.control.lane.events[k]))
                push(candidate.control.lane.events[k]);
        for (std::size_t op = 0; op < laneCount; ++op)
            if ((perms.operationLocks[op] & lock::events) != 0)
                merged.loopBeats[op] = source.control.lane.loopBeats[op];
        ++restored;
        candidate.control.lane = merged;
    }
    auto placementWithoutGrid = [](PlacementConfig p) { p.subdivision = 4; return p; };
    if (perms.placementLocked && !sameControl(SceneControl{placementWithoutGrid(source.control.placement), {}, {}, {}},
                                              SceneControl{placementWithoutGrid(candidate.control.placement), {}, {}, {}}))
    {
        const auto snap = candidate.control.placement.subdivision; // editor grid, not musical content
        candidate.control.placement = source.control.placement;
        candidate.control.placement.subdivision = snap;
        ++restored;
    }
    if (!keepCandidatePermissions)
        candidate.control.permissions = source.control.permissions;
    return restored;
}
} // namespace groovemorph::core::control
