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
                                 bool keepCandidatePermissions, std::int64_t phraseTicks) noexcept
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
    // Events lock (per operation) and per-event locks. A source event is protected when its own
    // `locked` flag or its operation's Events lock is set. A candidate event "is" a protected source
    // event when every field except `locked` matches: the flag itself is lock configuration, so it
    // follows keepCandidatePermissions like the operation lock bits do (toggling it never loses the event).
    const auto& srcLane = source.control.lane;
    auto& candLane = candidate.control.lane;
    const auto opEventsLocked = [&perms](std::uint8_t op) {
        return (perms.operationLocks[op % laneCount] & lock::events) != 0;
    };
    const auto isProtected = [&opEventsLocked](const LaneEvent& e) { return e.locked || opEventsLocked(e.operation); };
    const auto sameIgnoringLock = [](const LaneEvent& a, const LaneEvent& b) {
        return a.operation == b.operation && a.onsetTicks == b.onsetTicks && a.durationTicks == b.durationTicks
               && a.depth == b.depth && a.sliceBeats == b.sliceBeats && a.priority == b.priority
               && a.enabled == b.enabled && a.restartCycle == b.restartCycle && a.chance == b.chance;
    };
    const auto overlaps = [phraseTicks](const LaneEvent& a, const LaneEvent& b) {
        if (a.operation != b.operation)
            return false;
        if (phraseTicks > 0)
            return eventsOverlap(a, b, phraseTicks);
        return a.onsetTicks < static_cast<std::int64_t>(b.onsetTicks) + b.durationTicks
               && b.onsetTicks < static_cast<std::int64_t>(a.onsetTicks) + a.durationTicks;
    };
    bool anyProtected = false;
    for (std::size_t k = 0; k < srcLane.count && k < maxLaneEvents && !anyProtected; ++k)
        anyProtected = isProtected(srcLane.events[k]);
    for (std::uint8_t op = 0; op < laneCount && !anyProtected; ++op)
        anyProtected = opEventsLocked(op);
    if (anyProtected)
    {
        const std::size_t srcCount = std::min<std::size_t>(srcLane.count, maxLaneEvents);
        const std::size_t candCount = std::min<std::size_t>(candLane.count, maxLaneEvents);
        // Match every protected source event to a distinct candidate event.
        std::array<int, maxLaneEvents> matchOf{}; // candidate index for each source event, -1 none
        matchOf.fill(-1);
        std::array<bool, maxLaneEvents> candMatched{};
        bool differ = false;
        for (std::size_t i = 0; i < srcCount; ++i)
        {
            if (!isProtected(srcLane.events[i]))
                continue;
            for (std::size_t j = 0; j < candCount; ++j)
                if (!candMatched[j] && sameIgnoringLock(srcLane.events[i], candLane.events[j])
                    && (keepCandidatePermissions || srcLane.events[i].locked == candLane.events[j].locked))
                {
                    candMatched[j] = true;
                    matchOf[i] = static_cast<int>(j);
                    break;
                }
            differ = differ || matchOf[i] < 0;
        }
        // Anything else a candidate adds to a locked operation, or places over a protected event, differs.
        for (std::size_t j = 0; j < candCount && !differ; ++j)
        {
            if (candMatched[j])
                continue;
            differ = opEventsLocked(candLane.events[j].operation);
            for (std::size_t i = 0; i < srcCount && !differ; ++i)
                differ = isProtected(srcLane.events[i]) && overlaps(srcLane.events[i], candLane.events[j]);
        }
        if (differ)
        {
            EventLane merged;
            merged.loopBeats = candLane.loopBeats;
            merged.chanceByOp = candLane.chanceByOp;
            merged.chanceSeed = candLane.chanceSeed;
            merged.explicitEvents = srcLane.explicitEvents || candLane.explicitEvents;
            const auto push = [&merged](const LaneEvent& e) {
                if (merged.count < maxLaneEvents)
                    merged.events[merged.count++] = e;
            };
            // Protected events are mandatory, so they are placed first and can never be crowded out.
            for (std::size_t i = 0; i < srcCount; ++i)
                if (isProtected(srcLane.events[i]))
                {
                    LaneEvent e = srcLane.events[i];
                    if (keepCandidatePermissions && matchOf[i] >= 0)
                        e.locked = candLane.events[static_cast<std::size_t>(matchOf[i])].locked;
                    push(e);
                }
            for (std::size_t j = 0; j < candCount; ++j)
            {
                if (candMatched[j] || opEventsLocked(candLane.events[j].operation))
                    continue;
                bool blocked = false;
                for (std::size_t i = 0; i < srcCount && !blocked; ++i)
                    blocked = isProtected(srcLane.events[i]) && overlaps(srcLane.events[i], candLane.events[j]);
                if (!blocked)
                    push(candLane.events[j]);
            }
            for (std::size_t op = 0; op < laneCount; ++op)
                if (opEventsLocked(static_cast<std::uint8_t>(op)))
                {
                    merged.loopBeats[op] = srcLane.loopBeats[op];
                    merged.chanceByOp[op] = srcLane.chanceByOp[op];
                }
            ++restored;
            candLane = merged;
        }
    }
    // Loop lengths and the explicit-events switch decide how protected events sound; keep them too.
    if (anyProtected)
    {
        for (std::uint8_t op = 0; op < laneCount; ++op)
        {
            bool opProtected = opEventsLocked(op);
            for (std::size_t k = 0; k < srcLane.count && k < maxLaneEvents && !opProtected; ++k)
                opProtected = srcLane.events[k].operation == op && srcLane.events[k].locked;
            if (opProtected && candLane.loopBeats[op] != srcLane.loopBeats[op])
            {
                candLane.loopBeats[op] = srcLane.loopBeats[op];
                ++restored;
            }
            if (opEventsLocked(op) && candLane.chanceByOp[op] != srcLane.chanceByOp[op]) // Events lock only: a single locked event does not freeze the lane
            {
                candLane.chanceByOp[op] = srcLane.chanceByOp[op];
                ++restored;
            }
        }
        if (srcLane.explicitEvents && !candLane.explicitEvents)
        {
            candLane.explicitEvents = true;
            ++restored;
        }
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
