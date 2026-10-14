#include "core/control/SceneControl.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::control
{
namespace
{
bool clampFloat(float& v, float lo, float hi, float fallback) noexcept
{
    const float original = v;
    v = std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    return !(v == original);
}

bool sameEvent(const LaneEvent& a, const LaneEvent& b) noexcept
{
    return a.operation == b.operation && a.enabled == b.enabled && a.onsetTicks == b.onsetTicks
           && a.durationTicks == b.durationTicks && a.depth == b.depth;
}

bool validEvent(const LaneEvent& e, std::int64_t phraseTicks) noexcept
{
    return e.operation < laneCount && std::isfinite(e.depth) && e.depth >= 0.0f && e.depth <= 1.0f
           && e.onsetTicks >= 0 && e.onsetTicks < phraseTicks && e.durationTicks >= 1
           && e.durationTicks <= phraseTicks;
}
} // namespace

bool sanitize(SceneControl& c, std::int32_t) noexcept
{
    bool changed = false;
    auto& p = c.placement;
    if (static_cast<std::uint8_t>(p.window) > 2) { p.window = WindowMode::FullPhrase; changed = true; }
    changed = clampFloat(p.startPhase, 0.0f, 1.0f, 0.0f) || changed;
    if (p.startPhase >= 1.0f) { p.startPhase = 0.0f; changed = true; }
    changed = clampFloat(p.spanPhase, 1.0f / 1966080.0f, 1.0f, 1.0f) || changed;
    if (p.every < 1 || p.every > maxEvery) { p.every = std::clamp<std::uint8_t>(p.every, 1, maxEvery); changed = true; }
    if (p.cadenceOffset > maxEvery - 1) { p.cadenceOffset = maxEvery - 1; changed = true; }
    constexpr std::int32_t originLimit = 1966080;
    const auto origin = std::clamp(p.originTicks, -originLimit, originLimit);
    if (origin != p.originTicks) { p.originTicks = origin; changed = true; }
    constexpr std::uint8_t allowed[] = {1, 2, 3, 4, 6, 8, 12, 16};
    if (std::find(std::begin(allowed), std::end(allowed), p.subdivision) == std::end(allowed))
    {
        p.subdivision = 4;
        changed = true;
    }
    changed = clampFloat(p.downbeatProtectBeats, 0.0f, 4.0f, 0.0f) || changed;

    auto& lane = c.lane;
    if (lane.count > maxLaneEvents) { lane.count = static_cast<std::uint8_t>(maxLaneEvents); changed = true; }
    for (std::size_t i = 0; i < lane.count; ++i)
    {
        auto& e = lane.events[i];
        if (e.operation >= laneCount) { e.operation = 0; changed = true; }
        if (e.onsetTicks < 0) { e.onsetTicks = 0; changed = true; }
        if (e.durationTicks < 1) { e.durationTicks = 1; changed = true; }
        changed = clampFloat(e.depth, 0.0f, 1.0f, 1.0f) || changed;
    }
    for (auto& l : c.permissions.operationLocks)
        if ((l & ~lock::all) != 0) { l &= lock::all; changed = true; }
    auto& a = c.activity;
    if (a.maxConcurrent < 1 || a.maxConcurrent > laneCount)
    {
        a.maxConcurrent = std::clamp<std::uint8_t>(a.maxConcurrent, 1, laneCount);
        changed = true;
    }
    for (auto& pr : a.priority)
        if (pr > 100) { pr = 100; changed = true; }
    return changed;
}

bool sameControl(const SceneControl& a, const SceneControl& b) noexcept
{
    const auto &p = a.placement, &q = b.placement;
    if (p.enabled != q.enabled || p.window != q.window || p.startPhase != q.startPhase
        || p.spanPhase != q.spanPhase || p.every != q.every || p.cadenceOffset != q.cadenceOffset
        || p.originTicks != q.originTicks || p.subdivision != q.subdivision
        || p.downbeatProtectBeats != q.downbeatProtectBeats)
        return false;
    if (a.lane.explicitEvents != b.lane.explicitEvents || a.lane.count != b.lane.count)
        return false;
    for (std::size_t i = 0; i < a.lane.count && i < maxLaneEvents; ++i)
        if (!sameEvent(a.lane.events[i], b.lane.events[i]))
            return false;
    if (a.permissions.operationLocks != b.permissions.operationLocks
        || a.permissions.placementLocked != b.permissions.placementLocked)
        return false;
    return a.activity.enabled == b.activity.enabled && a.activity.maxConcurrent == b.activity.maxConcurrent
           && a.activity.priority == b.activity.priority;
}

bool isDefault(const SceneControl& control) noexcept
{
    return sameControl(control, SceneControl{});
}

bool eventsOverlap(const LaneEvent& a, const LaneEvent& b, std::int64_t phraseTicks) noexcept
{
    if (phraseTicks <= 0)
        return true;
    // Circular interval intersection: b.onset lies in a, or a.onset lies in b.
    auto inside = [phraseTicks](const LaneEvent& x, std::int64_t t) {
        std::int64_t d = (t - x.onsetTicks) % phraseTicks;
        if (d < 0)
            d += phraseTicks;
        return d < x.durationTicks;
    };
    return inside(a, b.onsetTicks) || inside(b, a.onsetTicks);
}

EditStatus addEvent(EventLane& lane, const LaneEvent& event, std::int64_t phraseTicks) noexcept
{
    if (!validEvent(event, phraseTicks))
        return EditStatus::Invalid;
    if (lane.count >= maxLaneEvents)
        return EditStatus::Capacity;
    for (std::size_t i = 0; i < lane.count; ++i)
        if (lane.events[i].operation == event.operation
            && eventsOverlap(lane.events[i], event, phraseTicks))
            return EditStatus::Overlap;
    lane.events[lane.count++] = event;
    return EditStatus::Ok;
}

EditStatus replaceEvent(EventLane& lane, std::size_t index, const LaneEvent& event,
                        std::int64_t phraseTicks) noexcept
{
    if (index >= lane.count || !validEvent(event, phraseTicks))
        return EditStatus::Invalid;
    for (std::size_t i = 0; i < lane.count; ++i)
        if (i != index && lane.events[i].operation == event.operation
            && eventsOverlap(lane.events[i], event, phraseTicks))
            return EditStatus::Overlap;
    lane.events[index] = event;
    return EditStatus::Ok;
}

bool removeEvent(EventLane& lane, std::size_t index) noexcept
{
    if (index >= lane.count)
        return false;
    for (std::size_t i = index; i + 1 < lane.count; ++i)
        lane.events[i] = lane.events[i + 1];
    lane.events[--lane.count] = LaneEvent{};
    return true;
}

std::size_t convertLegacyToEvents(EventLane& lane, std::uint32_t enabledMask,
                                  std::int64_t phraseTicks) noexcept
{
    std::size_t added = 0;
    for (std::uint8_t op = 0; op < laneCount; ++op)
    {
        if ((enabledMask & (1u << op)) == 0)
            continue;
        LaneEvent e;
        e.operation = op;
        e.onsetTicks = 0;
        e.durationTicks = static_cast<std::int32_t>(std::min<std::int64_t>(phraseTicks, 1966080));
        if (addEvent(lane, e, phraseTicks) == EditStatus::Ok)
            ++added;
    }
    lane.explicitEvents = true;
    return added;
}
} // namespace groovemorph::core::control
