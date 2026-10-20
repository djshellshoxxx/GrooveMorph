#include "core/variation/CandidateBuilder.h"

#include "core/control/VariationBlend.h"
#include "core/scene/SceneBank.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace groovemorph::core::variation
{
namespace
{
std::uint64_t next(std::uint64_t& state) noexcept
{
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

bool overlapsProtectedRegion(const control::PlacementConfig& placement,
                             const control::LaneEvent& event, std::int64_t phraseTicks) noexcept
{
    const auto overlapsLinear = [](std::int64_t aStart, std::int64_t aEnd,
                                   std::int64_t bStart, std::int64_t bEnd) {
        return aStart < bEnd && bStart < aEnd;
    };
    const auto overlapsCircular = [phraseTicks, &overlapsLinear](std::int64_t aStart,
                                                                  std::int64_t aLength,
                                                                  std::int64_t bStart,
                                                                  std::int64_t bLength) {
        if (aLength <= 0 || bLength <= 0 || phraseTicks <= 0)
            return false;
        aLength = std::min(aLength, phraseTicks);
        bLength = std::min(bLength, phraseTicks);
        aStart %= phraseTicks;
        bStart %= phraseTicks;
        if (aStart < 0) aStart += phraseTicks;
        if (bStart < 0) bStart += phraseTicks;
        const auto aEnd = aStart + aLength;
        const auto bEnd = bStart + bLength;
        const auto aFirstEnd = std::min(aEnd, phraseTicks);
        const auto bFirstEnd = std::min(bEnd, phraseTicks);
        if (overlapsLinear(aStart, aFirstEnd, bStart, bFirstEnd))
            return true;
        if (aEnd > phraseTicks && overlapsLinear(0, aEnd - phraseTicks, bStart, bFirstEnd))
            return true;
        if (bEnd > phraseTicks && overlapsLinear(aStart, aFirstEnd, 0, bEnd - phraseTicks))
            return true;
        return aEnd > phraseTicks && bEnd > phraseTicks
               && overlapsLinear(0, aEnd - phraseTicks, 0, bEnd - phraseTicks);
    };

    for (std::size_t i = 0; i < placement.regionCount && i < control::maxProtectedRegions; ++i)
    {
        const auto& region = placement.regions[i];
        if (overlapsCircular(event.onsetTicks, event.durationTicks,
                             region.startTicks, region.lengthTicks))
            return true;
    }
    return false;
}

void generateEventPatterns(const VariationSettings& source, const GeneratorSettings& settings,
                           Candidate& candidate) noexcept
{
    if (!settings.generateEventPatterns || source.control.permissions.placementLocked
        || !std::isfinite(settings.distance) || settings.distance <= 0.0f)
        return;

    constexpr std::int64_t maxPhraseTicks = 16 * 32 * control::ticksPerBeat;
    const auto phraseTicks = std::clamp<std::int64_t>(settings.phraseTicks,
                                                       control::ticksPerBeat, maxPhraseTicks);
    auto& lane = candidate.settings.control.lane;
    const auto sourceLane = source.control.lane;
    control::EventLane generated{};
    generated.explicitEvents = true;
    generated.loopBeats = sourceLane.loopBeats;

    // Keep event-locked lanes and individually locked events exactly. Other
    // unlocked patterns are intentionally replaced by the generated candidate.
    for (std::size_t i = 0; i < sourceLane.count && i < control::maxLaneEvents; ++i)
    {
        const auto& event = sourceLane.events[i];
        const auto op = static_cast<std::size_t>(event.operation);
        const bool opLocked = op >= control::laneCount
                              || (source.control.permissions.operationLocks[op] & control::lock::events) != 0
                              || (settings.protectedOperations & (1u << op)) != 0;
        if (event.locked || opLocked)
        {
            if (generated.count < control::maxLaneEvents)
                generated.events[generated.count++] = event;
        }
    }

    const auto distance = std::clamp(settings.distance, 0.0f, 1.0f);
    const int styleEventCount = settings.style == Style::Conservative ? 1
                                : settings.style == Style::Creative ? 2 : 3;
    const int eventCount = std::max(1, static_cast<int>(std::lround(styleEventCount * distance)));
    const int subdivision = std::clamp<int>(source.control.placement.subdivision, 1, 16);
    const auto snapTicks = std::max<std::int64_t>(1, control::ticksPerBeat / subdivision);
    constexpr std::int32_t durationChoices[] = {
        control::ticksPerBeat / 4, control::ticksPerBeat / 2, control::ticksPerBeat
    };
    constexpr float sliceChoices[] = {0.0f, 0.125f, 0.25f, 0.5f, 1.0f};

    for (std::size_t op = 0; op < operationCount; ++op)
    {
        const auto opBit = static_cast<std::uint32_t>(1u << op);
        const auto locks = source.control.permissions.operationLocks[op];
        if (!candidate.settings.operations[op].enabled
            || (settings.protectedOperations & opBit) != 0
            || (locks & control::lock::events) != 0)
            continue;

        std::uint64_t rng = settings.seed ^ (static_cast<std::uint64_t>(settings.candidate) << 32)
                            ^ (0x6a09e667f3bcc909ULL * static_cast<std::uint64_t>(op + 1));
        const auto slots = std::max<std::int64_t>(1, (phraseTicks + snapTicks - 1) / snapTicks);
        for (int n = 0; n < eventCount && generated.count < control::maxLaneEvents; ++n)
        {
            bool added = false;
            // Fixed upper bound keeps generation predictable even with dense locks/regions.
            for (int attempt = 0; attempt < 64 && !added; ++attempt)
            {
                const auto onset = static_cast<std::int64_t>(next(rng) % static_cast<std::uint64_t>(slots)) * snapTicks;
                if (onset >= phraseTicks)
                    continue;
                const auto duration = static_cast<std::int64_t>(durationChoices[next(rng) % 3u]);
                if (onset + duration > phraseTicks)
                    continue;

                control::LaneEvent event;
                event.operation = static_cast<std::uint8_t>(op);
                event.onsetTicks = static_cast<std::int32_t>(onset);
                event.durationTicks = static_cast<std::int32_t>(duration);
                event.depth = std::clamp(0.45f + static_cast<float>((next(rng) >> 40) * (0.55 / 16777216.0)),
                                         0.0f, 1.0f);
                event.sliceBeats = sliceChoices[next(rng) % 5u];
                event.priority = source.control.activity.priority[op];
                event.restartCycle = settings.style == Style::Experimental && (next(rng) & 3u) == 0;
                if (overlapsProtectedRegion(source.control.placement, event, phraseTicks))
                    continue;
                if (control::addEvent(generated, event, phraseTicks) == control::EditStatus::Ok)
                    added = true;
            }
        }
    }

    lane = generated;
}
} // namespace

Candidate buildCandidate(const VariationSettings& source, const GeneratorSettings& settings) noexcept
{
    Request request;
    request.source = source;
    request.seed = settings.seed;
    request.style = settings.style;
    request.candidateIndex = settings.candidate;
    request.protectedOperations = settings.protectedOperations;
    const auto planned = generate(request);

    Candidate out;
    out.settings = control::blendDistance(source, planned.settings, settings.distance);
    out.permissionRestores = control::enforcePermissions(source, out.settings);
    generateEventPatterns(source, settings, out);
    scene::sanitize(out.settings);
    for (std::size_t i = 0; i < operationCount; ++i)
        if (!scene::sameSettings(source.operations[i], out.settings.operations[i]))
            out.changedOperations |= 1u << i;
    out.changedEvents = !control::sameControl(source.control, out.settings.control);
    return out;
}
} // namespace groovemorph::core::variation
