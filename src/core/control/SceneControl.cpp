#include "core/control/SceneControl.h"
#include "core/control/PhaseResolver.h"

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
           && a.durationTicks == b.durationTicks && a.depth == b.depth
           && a.sliceBeats == b.sliceBeats && a.priority == b.priority
           && a.restartCycle == b.restartCycle && a.locked == b.locked;
}

bool validEvent(const LaneEvent& e, std::int64_t phraseTicks) noexcept
{
    return e.operation < laneCount && std::isfinite(e.depth) && e.depth >= 0.0f && e.depth <= 1.0f
           && std::isfinite(e.sliceBeats) && e.sliceBeats >= 0.0f && e.sliceBeats <= 4.0f
           && e.priority <= 100
           && e.onsetTicks >= 0 && e.onsetTicks < phraseTicks && e.durationTicks >= 1
           && e.durationTicks <= phraseTicks;
}
} // namespace

bool sanitize(SceneControl& c, std::int32_t phraseTicksHint) noexcept
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
    if (p.regionCount > maxProtectedRegions) { p.regionCount = static_cast<std::uint8_t>(maxProtectedRegions); changed = true; }
    for (std::size_t i = 0; i < p.regionCount; ++i)
    {
        auto& r = p.regions[i];
        const std::int32_t limit = phraseTicksHint > 0 ? phraseTicksHint : 1966080;
        if (r.startTicks < 0) { r.startTicks = 0; changed = true; }
        if (r.startTicks >= limit) { r.startTicks = limit - 1; changed = true; }
        if (r.lengthTicks < 1) { r.lengthTicks = 1; changed = true; }
        if (r.lengthTicks > limit) { r.lengthTicks = limit; changed = true; }
    }
    for (std::size_t i = p.regionCount; i < maxProtectedRegions; ++i)
        if (p.regions[i].startTicks != 0 || p.regions[i].lengthTicks != 0)
        {
            p.regions[i] = ProtectedRegion{};
            changed = true;
        }
    changed = sanitize(c.phase) || changed;

    auto& lane = c.lane;
    if (lane.count > maxLaneEvents) { lane.count = static_cast<std::uint8_t>(maxLaneEvents); changed = true; }
    for (std::size_t i = 0; i < lane.count; ++i)
    {
        auto& e = lane.events[i];
        if (e.operation >= laneCount) { e.operation = 0; changed = true; }
        if (e.onsetTicks < 0) { e.onsetTicks = 0; changed = true; }
        if (e.durationTicks < 1) { e.durationTicks = 1; changed = true; }
        changed = clampFloat(e.depth, 0.0f, 1.0f, 1.0f) || changed;
        changed = clampFloat(e.sliceBeats, 0.0f, 4.0f, 0.0f) || changed;
        if (e.sliceBeats > 0.0f && e.sliceBeats < 1.0f / 16.0f) { e.sliceBeats = 1.0f / 16.0f; changed = true; }
        if (e.priority > 100) { e.priority = 100; changed = true; }
    }
    for (auto& l : lane.loopBeats)
        if (l > 64) { l = 64; changed = true; }
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
    changed = clampFloat(a.minSpacingBeats, 0.0f, 4.0f, 0.0f) || changed;
    changed = clampFloat(a.maxCoverage, 0.0f, 1.0f, 1.0f) || changed;
    return changed;
}

bool sameControl(const SceneControl& a, const SceneControl& b) noexcept
{
    const auto &p = a.placement, &q = b.placement;
    if (p.enabled != q.enabled || p.window != q.window || p.startPhase != q.startPhase
        || p.spanPhase != q.spanPhase || p.every != q.every || p.cadenceOffset != q.cadenceOffset
        || p.originTicks != q.originTicks || p.subdivision != q.subdivision
        || p.downbeatProtectBeats != q.downbeatProtectBeats || p.regionCount != q.regionCount)
        return false;
    for (std::size_t i = 0; i < p.regionCount && i < maxProtectedRegions; ++i)
        if (p.regions[i].startTicks != q.regions[i].startTicks || p.regions[i].lengthTicks != q.regions[i].lengthTicks)
            return false;
    if (!samePhase(a.phase, b.phase))
        return false;
    if (a.lane.explicitEvents != b.lane.explicitEvents || a.lane.count != b.lane.count
        || a.lane.loopBeats != b.lane.loopBeats)
        return false;
    for (std::size_t i = 0; i < a.lane.count && i < maxLaneEvents; ++i)
        if (!sameEvent(a.lane.events[i], b.lane.events[i]))
            return false;
    if (a.permissions.operationLocks != b.permissions.operationLocks
        || a.permissions.placementLocked != b.permissions.placementLocked)
        return false;
    return a.activity.enabled == b.activity.enabled && a.activity.maxConcurrent == b.activity.maxConcurrent
           && a.activity.priority == b.activity.priority
           && a.activity.minSpacingBeats == b.activity.minSpacingBeats
           && a.activity.maxCoverage == b.activity.maxCoverage;
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

namespace
{
// Ring (ticks) on which an operation's events repeat: its own loop length, else the phrase.
std::int64_t laneRing(const EventLane& lane, std::uint8_t operation, std::int64_t phraseTicks) noexcept
{
    const std::uint8_t loop = lane.loopBeats[operation % laneCount];
    return loop > 0 ? static_cast<std::int64_t>(loop) * ticksPerBeat : phraseTicks;
}
} // namespace

EditStatus addEvent(EventLane& lane, const LaneEvent& event, std::int64_t phraseTicks) noexcept
{
    if (!validEvent(event, phraseTicks))
        return EditStatus::Invalid;
    if (lane.count >= maxLaneEvents)
        return EditStatus::Capacity;
    for (std::size_t i = 0; i < lane.count; ++i)
        if (lane.events[i].operation == event.operation
            && eventsOverlap(lane.events[i], event, laneRing(lane, event.operation, phraseTicks)))
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
            && eventsOverlap(lane.events[i], event, laneRing(lane, event.operation, phraseTicks)))
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

ScaleReport scaleEvents(EventLane& lane, std::int64_t numerator, std::int64_t denominator,
                        std::int64_t phraseTicks, const PermissionConfig& permissions) noexcept
{
    ScaleReport report;
    if (numerator < 1 || denominator < 1 || numerator > 64 || denominator > 64 || phraseTicks <= 0)
        return report;
    EventLane scaled;
    scaled.explicitEvents = lane.explicitEvents;
    scaled.loopBeats = lane.loopBeats;
    const auto isLocked = [&permissions](const LaneEvent& e) {
        return e.locked || (permissions.operationLocks[e.operation % laneCount] & lock::events) != 0;
    };
    // Locked events keep their exact position and are placed first so scaled events cannot crowd them out.
    for (std::size_t i = 0; i < lane.count && i < maxLaneEvents; ++i)
        if (isLocked(lane.events[i]))
        {
            scaled.events[scaled.count++] = lane.events[i];
            ++report.skippedLocked;
        }
    for (std::size_t i = 0; i < lane.count && i < maxLaneEvents; ++i)
    {
        const auto& e = lane.events[i];
        if (isLocked(e))
            continue;
        LaneEvent out = e;
        const auto round = [numerator, denominator](std::int64_t v) {
            return (v * numerator + denominator / 2) / denominator;
        };
        const std::int64_t onset = round(e.onsetTicks);
        out.onsetTicks = static_cast<std::int32_t>(std::min<std::int64_t>(onset, phraseTicks - 1));
        out.durationTicks = static_cast<std::int32_t>(
            std::clamp<std::int64_t>(round(e.durationTicks), 1, std::min<std::int64_t>(phraseTicks, 1966080)));
        if (onset >= phraseTicks)
        {
            ++report.dropped; // would start after the phrase ends
            continue;
        }
        bool overlaps = false;
        for (std::size_t k = 0; k < scaled.count && !overlaps; ++k)
            overlaps = scaled.events[k].operation == out.operation
                       && eventsOverlap(scaled.events[k], out, laneRing(scaled, out.operation, phraseTicks));
        if (overlaps)
        {
            ++report.dropped;
            continue;
        }
        scaled.events[scaled.count++] = out;
        ++report.scaled;
    }
    lane = scaled;
    return report;
}

std::size_t convertLegacyToEvents(EventLane& lane, std::uint32_t enabledMask,
                                  std::int64_t phraseTicks,
                                  const std::array<std::uint8_t, laneCount>* priorities) noexcept
{
    std::size_t added = 0;
    for (std::uint8_t op = 0; op < laneCount; ++op)
    {
        if ((enabledMask & (1u << op)) == 0)
            continue;
        LaneEvent e;
        e.operation = op;
        if (priorities != nullptr)
            e.priority = std::min<std::uint8_t>((*priorities)[op], 100); // keep the limit's lane priority
        e.onsetTicks = 0;
        e.durationTicks = static_cast<std::int32_t>(std::min<std::int64_t>(phraseTicks, 1966080));
        if (addEvent(lane, e, phraseTicks) == EditStatus::Ok)
            ++added;
    }
    lane.explicitEvents = true;
    return added;
}
} // namespace groovemorph::core::control

namespace groovemorph::core::control
{
namespace
{
std::int64_t circularGap(std::int64_t a, std::int64_t b, std::int64_t ring) noexcept
{
    std::int64_t d = (a - b) % ring;
    if (d < 0)
        d += ring;
    return std::min(d, ring - d);
}

// Union length (ticks) of enabled events whose bit is set in `mask` (bit i = event index i).
std::int64_t unionTicks(const EventLane& lane, std::uint32_t mask, std::int64_t ring) noexcept
{
    struct Span { std::int64_t a, b; };
    std::array<Span, maxLaneEvents * 2> spans{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < lane.count; ++i)
    {
        if ((mask & (1u << i)) == 0 || !lane.events[i].enabled)
            continue;
        const std::int64_t a = lane.events[i].onsetTicks;
        const std::int64_t b = a + lane.events[i].durationTicks;
        if (b <= ring)
            spans[n++] = {a, b};
        else
        {
            spans[n++] = {a, ring};
            spans[n++] = {0, b - ring};
        }
    }
    std::sort(spans.begin(), spans.begin() + static_cast<std::ptrdiff_t>(n),
              [](const Span& x, const Span& y) { return x.a < y.a; });
    std::int64_t total = 0, curA = 0, curB = -1;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (curB < spans[i].a)
        {
            if (curB >= 0)
                total += curB - curA;
            curA = spans[i].a;
            curB = spans[i].b;
        }
        else
            curB = std::max(curB, spans[i].b);
    }
    if (curB >= 0)
        total += curB - curA;
    return total;
}

std::uint8_t peakConcurrent(const EventLane& lane, std::uint32_t mask, std::int64_t ring) noexcept
{
    // Sample every event start: the maximum overlap of intervals occurs at some start.
    std::uint8_t peak = 0;
    for (std::size_t i = 0; i < lane.count; ++i)
    {
        if ((mask & (1u << i)) == 0 || !lane.events[i].enabled)
            continue;
        std::array<bool, laneCount> on{};
        for (std::size_t j = 0; j < lane.count; ++j)
        {
            if ((mask & (1u << j)) == 0 || !lane.events[j].enabled)
                continue;
            std::int64_t d = (lane.events[i].onsetTicks - lane.events[j].onsetTicks) % ring;
            if (d < 0)
                d += ring;
            if (d < lane.events[j].durationTicks)
                on[lane.events[j].operation % laneCount] = true;
        }
        std::uint8_t c = 0;
        for (bool b : on)
            c = static_cast<std::uint8_t>(c + (b ? 1 : 0));
        peak = std::max(peak, c);
    }
    return peak;
}
} // namespace

ReduceReport reduceEvents(EventLane& lane, const ActivityConfig& config,
                          const PermissionConfig& permissions, std::int64_t phraseTicks,
                          bool apply) noexcept
{
    ReduceReport report;
    if (phraseTicks <= 0 || lane.count == 0)
        return report;
    // Events saved for a longer phrase may lie beyond this one; the evaluator wraps them, so the
    // analysis works on wrapped, length-limited copies. Kept events are written back unmodified.
    const EventLane original = lane;
    for (std::size_t i = 0; i < lane.count && i < maxLaneEvents; ++i)
    {
        auto& e = lane.events[i];
        std::int64_t onset = static_cast<std::int64_t>(e.onsetTicks) % phraseTicks;
        if (onset < 0)
            onset += phraseTicks;
        e.onsetTicks = static_cast<std::int32_t>(onset);
        e.durationTicks = static_cast<std::int32_t>(std::clamp<std::int64_t>(e.durationTicks, 1, phraseTicks));
    }
    const std::int64_t spacing = static_cast<std::int64_t>(
        std::ceil(static_cast<double>(std::clamp(config.minSpacingBeats, 0.0f, 4.0f)) * ticksPerBeat));
    const std::int64_t coverageLimit = static_cast<std::int64_t>(
        std::floor(static_cast<double>(std::clamp(config.maxCoverage, 0.0f, 1.0f)) * static_cast<double>(phraseTicks)));

    std::array<std::size_t, maxLaneEvents> order{};
    std::size_t n = 0;
    std::uint32_t kept = 0;
    std::uint32_t mandatory = 0;
    for (std::size_t i = 0; i < lane.count; ++i)
        if (lane.events[i].locked
            || (permissions.operationLocks[lane.events[i].operation % laneCount] & lock::events) != 0)
            mandatory |= 1u << i;
    for (std::size_t i = 0; i < lane.count; ++i)
        if ((mandatory & (1u << i)) != 0)
            order[n++] = i;
    const std::size_t firstFree = n;
    for (std::size_t i = 0; i < lane.count; ++i)
        if ((mandatory & (1u << i)) == 0)
            order[n++] = i;
    std::stable_sort(order.begin() + static_cast<std::ptrdiff_t>(firstFree),
                     order.begin() + static_cast<std::ptrdiff_t>(n),
                     [&lane](std::size_t a, std::size_t b) {
                         return lane.events[a].priority > lane.events[b].priority;
                     });

    const auto violates = [&](std::uint32_t set, std::size_t candidate) {
        if (spacing > 0)
            for (std::size_t j = 0; j < lane.count; ++j)
                if ((set & (1u << j)) != 0 && j != candidate
                    && circularGap(lane.events[j].onsetTicks, lane.events[candidate].onsetTicks, phraseTicks) < spacing)
                    return true;
        return unionTicks(lane, set | (1u << candidate), phraseTicks) > coverageLimit;
    };

    for (std::size_t k = 0; k < n; ++k)
    {
        const std::size_t i = order[k];
        if ((mandatory & (1u << i)) != 0)
        {
            if (violates(kept, i))
            {
                report.status = ReduceStatus::ConflictLocked; // locked events alone break a limit
                lane = original;
                return report;
            }
            kept |= 1u << i;
        }
        else if (!violates(kept, i))
            kept |= 1u << i;
    }
    for (std::size_t i = 0; i < lane.count; ++i)
        ((kept & (1u << i)) != 0 ? report.kept : report.dropped)++;
    report.coverage = static_cast<float>(static_cast<double>(unionTicks(lane, kept, phraseTicks))
                                         / static_cast<double>(phraseTicks));
    report.peakConcurrent = peakConcurrent(lane, kept, phraseTicks);
    lane = original;
    if (apply && report.dropped > 0)
    {
        EventLane reduced = original;
        reduced.count = 0;
        for (std::size_t i = 0; i < original.count; ++i)
            if ((kept & (1u << i)) != 0)
                reduced.events[reduced.count++] = original.events[i];
        for (std::size_t i = reduced.count; i < maxLaneEvents; ++i)
            reduced.events[i] = LaneEvent{};
        lane = reduced;
    }
    return report;
}
} // namespace groovemorph::core::control
