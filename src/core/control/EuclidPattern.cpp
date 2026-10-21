#include "core/control/EuclidPattern.h"

#include "core/control/Chance.h"
#include "core/control/VariationBlend.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace groovemorph::core::control
{
namespace
{
constexpr std::uint8_t allowedSubdivisions[] = {1, 2, 3, 4, 6, 8, 12, 16};

bool eventsLockedFor(const VariationSettings& scene, std::uint8_t op) noexcept
{
    return (scene.control.permissions.operationLocks[op % laneCount] & lock::events) != 0;
}

std::int64_t ringTicks(const EventLane& lane, std::uint8_t op, std::int64_t phraseTicks) noexcept
{
    const std::uint8_t loop = lane.loopBeats[op % laneCount];
    return loop > 0 ? static_cast<std::int64_t>(loop) * ticksPerBeat : phraseTicks;
}

// Circular interval intersection of [aStart, aStart+aLen) and [bStart, bStart+bLen) on `ring`.
bool circularOverlap(std::int64_t aStart, std::int64_t aLen, std::int64_t bStart, std::int64_t bLen,
                     std::int64_t ring) noexcept
{
    if (ring <= 0 || aLen <= 0 || bLen <= 0)
        return false;
    const auto inside = [ring](std::int64_t start, std::int64_t len, std::int64_t t) {
        std::int64_t d = (t - start) % ring;
        if (d < 0)
            d += ring;
        return d < std::min(len, ring);
    };
    return inside(aStart, aLen, bStart) || inside(bStart, bLen, aStart);
}

bool hitsRegion(const PlacementConfig& placement, std::int64_t onset, std::int64_t length,
                std::int64_t phraseTicks) noexcept
{
    for (std::size_t i = 0; i < placement.regionCount && i < maxProtectedRegions; ++i)
    {
        const auto& r = placement.regions[i];
        if (circularOverlap(onset, length, r.startTicks, r.lengthTicks, phraseTicks))
            return true;
    }
    return false;
}

float cleanDepth(float v, float fallback) noexcept
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : fallback;
}

struct Group
{
    std::uint32_t bits = 0; // first element is the highest used bit
    int length = 0;
    int count = 0;
};

Group concat(const Group& a, const Group& b) noexcept
{
    return Group{(a.bits << b.length) | b.bits, a.length + b.length, 0};
}

// Removes unlocked events of `op`; returns {removed, keptLocked}.
std::pair<std::uint16_t, std::uint16_t> removeUnlocked(EventLane& lane, std::uint8_t op) noexcept
{
    std::uint16_t removed = 0, kept = 0;
    std::size_t out = 0;
    const std::size_t n = std::min<std::size_t>(lane.count, maxLaneEvents);
    for (std::size_t i = 0; i < n; ++i)
    {
        const LaneEvent e = lane.events[i];
        if (e.operation == op)
        {
            if (e.locked)
                ++kept;
            else
            {
                ++removed;
                continue;
            }
        }
        lane.events[out++] = e;
    }
    for (std::size_t i = out; i < n; ++i)
        lane.events[i] = LaneEvent{};
    lane.count = static_cast<decltype(lane.count)>(out);
    return {removed, kept};
}
} // namespace

bool sanitize(EuclidParams& p) noexcept
{
    const EuclidParams before = p;
    p.steps = std::clamp<std::uint8_t>(p.steps, 1, static_cast<std::uint8_t>(euclidMaxSteps));
    p.pulses = std::min(p.pulses, p.steps);
    p.operation = std::min<std::uint8_t>(p.operation, static_cast<std::uint8_t>(laneCount - 1));
    if (std::find(std::begin(allowedSubdivisions), std::end(allowedSubdivisions), p.subdivision)
        == std::end(allowedSubdivisions))
        p.subdivision = 4;
    p.gatePercent = std::clamp<std::uint8_t>(p.gatePercent, 10, 100);
    p.accentEvery = std::min<std::uint8_t>(p.accentEvery, static_cast<std::uint8_t>(euclidMaxSteps));
    p.accentDepth = cleanDepth(p.accentDepth, 1.0f);
    p.softDepth = cleanDepth(p.softDepth, 0.6f);
    p.density = std::min<std::uint8_t>(p.density, 100);
    return p.steps != before.steps || p.pulses != before.pulses || p.operation != before.operation
           || p.subdivision != before.subdivision || p.gatePercent != before.gatePercent
           || p.accentEvery != before.accentEvery || p.accentDepth != before.accentDepth
           || p.softDepth != before.softDepth || p.density != before.density;
}

EuclidSteps bjorklund(int steps, int pulses) noexcept
{
    EuclidSteps out{};
    steps = std::clamp(steps, 1, euclidMaxSteps);
    pulses = std::clamp(pulses, 0, steps);
    if (pulses == 0)
        return out;
    if (pulses == steps)
    {
        for (int i = 0; i < steps; ++i)
            out[static_cast<std::size_t>(i)] = true;
        return out;
    }

    // Group-merging Bjorklund: `a` groups of pattern A and `b` groups of pattern B; repeatedly pair
    // min(a,b) groups into A+B until at most one B group remains.
    Group a{1u, 1, pulses};
    Group b{0u, 1, steps - pulses};
    while (b.count > 1)
    {
        const int m = std::min(a.count, b.count);
        Group merged = concat(a, b);
        merged.count = m;
        Group rest = a.count > b.count ? a : b;
        rest.count = std::max(a.count, b.count) - m;
        a = merged;
        b = rest;
    }
    int index = 0;
    const auto emit = [&out, &index](const Group& g) {
        for (int n = 0; n < g.count; ++n)
            for (int bit = g.length - 1; bit >= 0; --bit)
                out[static_cast<std::size_t>(index++)] = ((g.bits >> bit) & 1u) != 0;
    };
    emit(a);
    emit(b);
    return out;
}

EuclidSteps euclidSteps(const EuclidParams& raw) noexcept
{
    EuclidParams p = raw;
    sanitize(p);
    const auto base = bjorklund(p.steps, p.pulses);
    EuclidSteps out{};
    const int n = p.steps;
    int r = static_cast<int>(raw.rotation) % n;
    if (r < 0)
        r += n;
    for (int i = 0; i < n; ++i)
        out[static_cast<std::size_t>(i)] = base[static_cast<std::size_t>(((i - r) % n + n) % n)];
    return out;
}

std::int32_t euclidStepTicks(std::uint8_t subdivision) noexcept
{
    if (std::find(std::begin(allowedSubdivisions), std::end(allowedSubdivisions), subdivision)
        == std::end(allowedSubdivisions))
        subdivision = 4;
    return ticksPerBeat / subdivision;
}

EuclidReport applyEuclid(VariationSettings& scene, const EuclidParams& raw, std::int64_t phraseTicks,
                         std::size_t sceneIndex) noexcept
{
    EuclidReport report;
    EuclidParams p = raw;
    sanitize(p);
    if (phraseTicks <= 0 || phraseTicks > maxPhraseTicks || raw.operation >= laneCount)
    {
        // A3-07: tile count is ring / tileTicks; an unbounded phrase overflowed it and could spin for hours.
        report.status = EuclidStatus::Invalid;
        return report;
    }
    if (eventsLockedFor(scene, p.operation))
    {
        report.status = EuclidStatus::OperationLocked;
        return report;
    }

    auto& lane = scene.control.lane;
    if (!lane.explicitEvents)
    {
        // Keep the other enabled operations sounding: legacy uniform becomes full-phrase events.
        std::uint32_t mask = 0;
        for (std::size_t i = 0; i < operationCount; ++i)
            if (scene.operations[i].enabled && i != p.operation)
                mask |= 1u << i;
        lane.count = 0;
        if (mask != 0)
        {
            convertLegacyToEvents(lane, mask, phraseTicks, &scene.control.activity.priority);
            report.convertedLegacy = true;
        }
        lane.explicitEvents = true;
    }

    const auto removedKept = removeUnlocked(lane, p.operation);
    report.removed = removedKept.first;
    report.keptLocked = removedKept.second;

    const auto steps = euclidSteps(p);
    const std::int64_t stepTicks = euclidStepTicks(p.subdivision);
    const std::int64_t ring = ringTicks(lane, p.operation, phraseTicks);
    const std::int64_t tileTicks = stepTicks * p.steps;
    const std::int64_t tiles = p.repeat ? (ring + tileTicks - 1) / tileTicks : 1;
    const bool accents = p.accentEvery > 0;
    bool full = false;
    bool usesChance = false;
    for (std::int64_t tile = 0; tile < tiles && !full; ++tile)
    {
        int pulseIndex = 0; // accent phase restarts with every tile so tiles are identical
        for (int s = 0; s < p.steps; ++s)
        {
            if (!steps[static_cast<std::size_t>(s)])
                continue;
            const bool accent = accents && (pulseIndex % p.accentEvery) == 0;
            ++pulseIndex;
            const std::int64_t onset = tile * tileTicks + static_cast<std::int64_t>(s) * stepTicks;
            if (onset >= ring)
                continue; // beyond the ring (partial last tile)
            if (full)
            {
                ++report.skippedCapacity;
                continue;
            }
            LaneEvent e;
            e.operation = p.operation;
            e.onsetTicks = static_cast<std::int32_t>(onset);
            e.durationTicks = static_cast<std::int32_t>(
                std::clamp<std::int64_t>(stepTicks * p.gatePercent / 100, 1, ring - onset));
            e.priority = scene.control.activity.priority[p.operation];
            e.depth = accents ? (accent ? p.accentDepth : p.softDepth) : p.accentDepth;
            e.chance = (accents && accent) ? std::uint8_t{100} : p.density;
            if (hitsRegion(scene.control.placement, e.onsetTicks, e.durationTicks, phraseTicks))
            {
                ++report.skippedRegion;
                continue;
            }
            switch (addEvent(lane, e, phraseTicks))
            {
            case EditStatus::Ok:
                ++report.added;
                usesChance = usesChance || e.chance < 100;
                break;
            case EditStatus::Capacity:
                ++report.skippedCapacity;
                full = true;
                break;
            default:
                ++report.skippedOverlap;
                break;
            }
        }
    }
    if (usesChance)
        ensureChanceSeed(lane, sceneIndex);
    return report;
}

VariationSettings buildEuclidCandidate(const VariationSettings& source, const EuclidParams& params,
                                       std::int64_t phraseTicks, EuclidReport* report,
                                       std::size_t sceneIndex) noexcept
{
    VariationSettings candidate = source;
    const auto r = applyEuclid(candidate, params, phraseTicks, sceneIndex);
    if (r.status == EuclidStatus::Ok)
        enforcePermissions(source, candidate, true, phraseTicks);
    else
        candidate = source;
    if (report != nullptr)
        *report = r;
    return candidate;
}

EuclidReport clearEuclidOperation(VariationSettings& scene, std::uint8_t operation) noexcept
{
    EuclidReport report;
    if (operation >= laneCount)
    {
        report.status = EuclidStatus::Invalid;
        return report;
    }
    if (eventsLockedFor(scene, operation))
    {
        report.status = EuclidStatus::OperationLocked;
        return report;
    }
    const auto removedKept = removeUnlocked(scene.control.lane, operation);
    report.removed = removedKept.first;
    report.keptLocked = removedKept.second;
    return report;
}

VariationSettings buildClearCandidate(const VariationSettings& source, std::uint8_t operation,
                                      std::int64_t phraseTicks, EuclidReport* report) noexcept
{
    VariationSettings candidate = source;
    const auto r = clearEuclidOperation(candidate, operation);
    if (r.status == EuclidStatus::Ok)
        enforcePermissions(source, candidate, true, phraseTicks);
    else
        candidate = source;
    if (report != nullptr)
        *report = r;
    return candidate;
}
} // namespace groovemorph::core::control
