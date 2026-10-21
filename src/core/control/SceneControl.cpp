#include "core/control/SceneControl.h"
#include "core/control/PhaseResolver.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

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
           && a.restartCycle == b.restartCycle && a.locked == b.locked && a.chance == b.chance;
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
    if (lane.count > maxLaneEvents) { lane.count = static_cast<std::uint16_t>(maxLaneEvents); changed = true; }
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
        if (e.chance > 100) { e.chance = 100; changed = true; }
    }
    for (auto& ch : lane.chanceByOp)
        if (ch > 100) { ch = 100; changed = true; }
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
        || a.lane.loopBeats != b.lane.loopBeats || a.lane.chanceByOp != b.lane.chanceByOp
        || a.lane.chanceSeed != b.lane.chanceSeed)
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
    scaled.chanceByOp = lane.chanceByOp;
    scaled.chanceSeed = lane.chanceSeed;
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


// ---------------------------------------------------------------------------------------------
// C06 Analyze / Reduce, loop-aware (docs/specs/W3-LOOP-REDUCE.md). Message thread only: it uses
// std::vector for the unrolled spans.
// ---------------------------------------------------------------------------------------------
namespace groovemorph::core::control
{
namespace
{
struct Span
{
    std::int64_t a;
    std::int64_t b;
};

struct Norm
{
    std::int64_t onset = 0;    // modulo the event's ring
    std::int64_t duration = 1; // capped at the ring
    std::int64_t ring = 1;
    std::uint8_t op = 0;
    bool enabled = true;
    bool loops = false;        // the operation has its own loop length
};

std::int64_t modPos(std::int64_t a, std::int64_t m) noexcept
{
    const std::int64_t r = a % m;
    return r < 0 ? r + m : r;
}

// Copies of the event on the first phrase [0, P): start = onset + k*ring (k >= -1), clipped.
void unroll(const Norm& e, std::int64_t P, std::vector<Span>& out)
{
    for (std::int64_t s = e.onset - e.ring; s < P; s += e.ring)
    {
        const std::int64_t a = std::max<std::int64_t>(s, 0);
        const std::int64_t b = std::min<std::int64_t>(s + e.duration, P);
        if (b > a)
            out.push_back({a, b});
    }
}

// Union of two sorted, disjoint span lists plus an unsorted list; returns the merged sorted list.
std::vector<Span> mergeSpans(const std::vector<Span>& base, std::vector<Span> extra)
{
    std::sort(extra.begin(), extra.end(), [](const Span& x, const Span& y) { return x.a < y.a; });
    std::vector<Span> out;
    out.reserve(base.size() + extra.size());
    std::size_t i = 0, j = 0;
    const auto push = [&out](const Span& s) {
        if (!out.empty() && s.a <= out.back().b)
            out.back().b = std::max(out.back().b, s.b);
        else
            out.push_back(s);
    };
    while (i < base.size() || j < extra.size())
    {
        if (j >= extra.size() || (i < base.size() && base[i].a <= extra[j].a))
            push(base[i++]);
        else
            push(extra[j++]);
    }
    return out;
}

std::int64_t totalLength(const std::vector<Span>& spans) noexcept
{
    std::int64_t total = 0;
    for (const auto& s : spans)
        total += s.b - s.a;
    return total;
}

// Closest distance between the starts of two periodic event copies (exact, integer).
std::int64_t startGap(const Norm& x, const Norm& y) noexcept
{
    const std::int64_t g = std::gcd(x.ring, y.ring);
    const std::int64_t m = modPos(x.onset - y.onset, g);
    return std::min(m, g - m);
}

std::uint8_t peakOperations(const std::vector<Span>* perEvent, const std::vector<Norm>& norm,
                            const std::array<bool, maxLaneEvents>& kept)
{
    struct Edge
    {
        std::int64_t pos;
        int delta;
        std::uint8_t op;
    };
    std::vector<Edge> edges;
    for (std::size_t i = 0; i < norm.size(); ++i)
    {
        if (!kept[i] || !norm[i].enabled)
            continue;
        for (const auto& s : perEvent[i])
        {
            edges.push_back({s.a, +1, norm[i].op});
            edges.push_back({s.b, -1, norm[i].op});
        }
    }
    std::sort(edges.begin(), edges.end(), [](const Edge& x, const Edge& y) {
        return x.pos != y.pos ? x.pos < y.pos : x.delta < y.delta; // ends before starts
    });
    std::array<int, laneCount> active{};
    int peak = 0;
    for (const auto& e : edges)
    {
        active[e.op % laneCount] += e.delta;
        int distinct = 0;
        for (int v : active)
            distinct += v > 0 ? 1 : 0;
        peak = std::max(peak, distinct);
    }
    return static_cast<std::uint8_t>(std::min(peak, static_cast<int>(laneCount)));
}
} // namespace

ReduceReport reduceEvents(EventLane& lane, const ActivityConfig& config,
                          const PermissionConfig& permissions, std::int64_t phraseTicks,
                          bool apply) noexcept
{
    ReduceReport report;
    if (phraseTicks <= 0 || phraseTicks > maxPhraseTicks || lane.count == 0)
        return report; // an out-of-range phrase would unroll a short loop into billions of spans (A3-03)
    const std::size_t count = std::min<std::size_t>(lane.count, maxLaneEvents);
    const std::int64_t P = phraseTicks;

    std::vector<Norm> norm(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& e = lane.events[i];
        auto& n = norm[i];
        n.op = static_cast<std::uint8_t>(e.operation % laneCount);
        n.loops = lane.loopBeats[n.op] > 0;
        n.ring = n.loops ? static_cast<std::int64_t>(lane.loopBeats[n.op]) * ticksPerBeat : P;
        n.onset = modPos(e.onsetTicks, n.ring);
        n.duration = std::clamp<std::int64_t>(e.durationTicks, 1, n.ring);
        n.enabled = e.enabled;
    }
    const std::int64_t spacing = static_cast<std::int64_t>(
        std::ceil(static_cast<double>(std::clamp(config.minSpacingBeats, 0.0f, 4.0f)) * ticksPerBeat));
    const std::int64_t coverageLimit = static_cast<std::int64_t>(
        std::floor(static_cast<double>(std::clamp(config.maxCoverage, 0.0f, 1.0f)) * static_cast<double>(P)));

    std::vector<std::vector<Span>> spans(count);
    for (std::size_t i = 0; i < count; ++i)
        if (norm[i].enabled)
            unroll(norm[i], P, spans[i]);

    std::array<bool, maxLaneEvents> mandatory{};
    std::array<bool, maxLaneEvents> kept{};
    for (std::size_t i = 0; i < count; ++i)
        mandatory[i] = lane.events[i].locked
                       || (permissions.operationLocks[norm[i].op] & lock::events) != 0;

    std::array<std::size_t, maxLaneEvents> order{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < count; ++i)
        if (mandatory[i])
            order[n++] = i;
    const std::size_t firstFree = n;
    for (std::size_t i = 0; i < count; ++i)
        if (!mandatory[i])
            order[n++] = i;
    std::stable_sort(order.begin() + static_cast<std::ptrdiff_t>(firstFree),
                     order.begin() + static_cast<std::ptrdiff_t>(n),
                     [&lane](std::size_t a, std::size_t b) {
                         return lane.events[a].priority > lane.events[b].priority;
                     });

    std::vector<Span> keptUnion;
    // Returns true when adding `candidate` breaks a limit; fills `merged` with the tentative union.
    const auto violates = [&](std::size_t candidate, std::vector<Span>& merged) {
        const auto& c = norm[candidate];
        if (!c.enabled)
        {
            merged = keptUnion;
            return false;
        }
        if (spacing > 0)
        {
            if (c.loops && c.ring < spacing)
                return true; // the lane repeats the event closer than the minimum spacing
            for (std::size_t j = 0; j < count; ++j)
                if (kept[j] && j != candidate && norm[j].enabled && startGap(norm[j], c) < spacing)
                    return true;
        }
        merged = mergeSpans(keptUnion, spans[candidate]);
        return totalLength(merged) > coverageLimit;
    };

    std::vector<Span> merged;
    for (std::size_t k = 0; k < n; ++k)
    {
        const std::size_t i = order[k];
        const bool bad = violates(i, merged);
        if (!bad)
        {
            kept[i] = true;
            keptUnion = merged;
        }
        else if (mandatory[i])
        {
            report.status = ReduceStatus::ConflictLocked; // locked events alone break a limit
            return report;
        }
    }
    for (std::size_t i = 0; i < count; ++i)
        (kept[i] ? report.kept : report.dropped)++;
    report.coverage = static_cast<float>(static_cast<double>(totalLength(keptUnion)) / static_cast<double>(P));
    report.peakConcurrent = peakOperations(spans.data(), norm, kept);
    if (apply && report.dropped > 0)
    {
        EventLane reduced = lane;
        reduced.count = 0;
        for (std::size_t i = 0; i < count; ++i)
            if (kept[i])
                reduced.events[reduced.count++] = lane.events[i];
        for (std::size_t i = reduced.count; i < maxLaneEvents; ++i)
            reduced.events[i] = LaneEvent{};
        lane = reduced;
    }
    return report;
}
} // namespace groovemorph::core::control
