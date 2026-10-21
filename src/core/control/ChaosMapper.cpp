#include "core/control/ChaosMapper.h"

#include "core/control/SceneControl.h"
#include "core/control/VariationBlend.h"
#include "core/scene/SceneBank.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace groovemorph::core::control
{
namespace
{
constexpr std::size_t candidatesPerOperation = 4;
constexpr std::uint64_t chaosSalt = 0x4348414F53ULL; // "CHAOS"

std::uint64_t next(std::uint64_t& state) noexcept
{
    state += 0x9e3779b97f4a7c15ULL;
    auto z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

float unit(std::uint64_t& state) noexcept
{
    return static_cast<float>((next(state) >> 40) * (1.0 / 16777216.0));
}

struct Candidate
{
    bool accepted = false;
    LaneEvent event{};
    float threshold = 2.0f; // included when chaos >= threshold
};
} // namespace

VariationSettings applyChaos(const VariationSettings& scene, const ChaosContext& context) noexcept
{
    // `!(x > 0)` also rejects NaN. The early return keeps chaos 0 bit-identical to the unmodified scene.
    if (!(context.chaos > 0.0f))
        return scene;
    const float chaos = std::min(1.0f, context.chaos);

    VariationSettings out = scene;
    const auto& perms = scene.control.permissions;
    const auto& lane = scene.control.lane;
    const auto& placement = scene.control.placement;
    const bool phraseValid = context.phraseTicks > 0 && context.phraseTicks <= (std::int64_t{1} << 40);

    // Events are only added to explicit lanes whose placement is not locked.
    const bool laneOpen = lane.explicitEvents && !perms.placementLocked && phraseValid;
    EventLane working = lane; // original events plus every accepted candidate, independent of chaos
    EventLane result = lane;  // original events plus the candidates included at this chaos
    const int subdivision = std::clamp<int>(placement.subdivision, 1, 16);
    const std::int64_t snapTicks = std::max<std::int32_t>(1, ticksPerBeat / subdivision);
    constexpr std::int32_t durationChoices[] = {ticksPerBeat / 4, ticksPerBeat / 2, ticksPerBeat};
    constexpr float sliceChoices[] = {0.0f, 0.125f, 0.25f, 0.5f, 1.0f};

    for (std::size_t op = 0; op < operationCount; ++op)
    {
        // Independent stream per operation: one lock or never-touch bit never shifts another operation's draws.
        // Draws are taken before any lock is consulted, so locks never change what the others receive.
        std::uint64_t rng = context.seed ^ chaosSalt;
        (void) next(rng);
        rng += (static_cast<std::uint64_t>(context.sceneIndex) + 1) * 0xd6e8feb86659fd93ULL;
        rng += (static_cast<std::uint64_t>(op) + 1) * 0xa24baed4963ee407ULL;
        (void) next(rng);
        const float uEnable = unit(rng);
        const float uDepth = unit(rng);
        const float uSlice = unit(rng);

        if ((context.neverTouchMask & (1u << op)) != 0)
            continue;

        const auto locks = perms.operationLocks[op];
        auto& o = out.operations[op];
        if (!o.enabled && (locks & lock::enable) == 0 && chaos >= 0.2f + 0.8f * uEnable)
            o.enabled = true;
        if (!o.enabled)
            continue;

        if ((locks & lock::depth) == 0)
        {
            const float d = std::isfinite(o.depth) ? std::clamp(o.depth, 0.0f, 1.0f) : 0.5f;
            o.depth = std::clamp(d + (1.0f - d) * chaos * (0.5f + 0.5f * uDepth), 0.0f, 1.0f);
        }
        if ((locks & lock::slice) == 0)
        {
            const float s = std::isfinite(o.sliceBeats) ? std::clamp(o.sliceBeats, 1.0f / 16.0f, 4.0f) : 0.25f;
            o.sliceBeats = std::max(1.0f / 16.0f, s * std::exp2(-2.0f * chaos * (0.5f + 0.5f * uSlice)));
        }
    }

    if (laneOpen)
    {
        // Candidate sequence: resolved for every eligible operation WITHOUT looking at the chaos value, so
        // the included set (thresholds only) grows monotonically with chaos.
        for (std::size_t op = 0; op < operationCount; ++op)
        {
            std::uint64_t rng = context.seed ^ chaosSalt;
            (void) next(rng);
            rng += (static_cast<std::uint64_t>(context.sceneIndex) + 1) * 0xd6e8feb86659fd93ULL;
            rng += (static_cast<std::uint64_t>(op) + 1) * 0xa24baed4963ee407ULL;
            (void) next(rng);
            (void) unit(rng);
            (void) unit(rng);
            (void) unit(rng);

            const auto locks = perms.operationLocks[op];
            const bool eligible = (context.neverTouchMask & (1u << op)) == 0
                                  && (locks & lock::events) == 0
                                  && (scene.operations[op].enabled || (locks & lock::enable) == 0);
            std::array<Candidate, candidatesPerOperation> candidates{};
            for (std::size_t k = 0; k < candidatesPerOperation; ++k)
            {
                const auto a = next(rng);
                const auto b = next(rng);
                const auto c = next(rng);
                const auto d = next(rng);
                const auto e = next(rng);
                if (!eligible)
                    continue;
                const std::int64_t slots = std::max<std::int64_t>(1, (context.phraseTicks + snapTicks - 1) / snapTicks);
                const std::int64_t onset = static_cast<std::int64_t>(a % static_cast<std::uint64_t>(slots)) * snapTicks;
                const std::int64_t duration = durationChoices[b % 3u];
                if (onset + duration > context.phraseTicks)
                    continue;
                LaneEvent event;
                event.operation = static_cast<std::uint8_t>(op);
                event.onsetTicks = static_cast<std::int32_t>(onset);
                event.durationTicks = static_cast<std::int32_t>(duration);
                event.depth = std::clamp(0.45f + 0.55f * static_cast<float>((c >> 40) * (1.0 / 16777216.0)), 0.0f, 1.0f);
                event.sliceBeats = sliceChoices[d % 5u];
                event.priority = scene.control.activity.priority[op];
                bool blocked = false;
                for (std::size_t r = 0; r < placement.regionCount && r < maxProtectedRegions && !blocked; ++r)
                {
                    LaneEvent region;
                    region.operation = event.operation;
                    region.onsetTicks = placement.regions[r].startTicks;
                    region.durationTicks = std::max<std::int32_t>(1, placement.regions[r].lengthTicks);
                    blocked = eventsOverlap(event, region, context.phraseTicks);
                }
                if (blocked || addEvent(working, event, context.phraseTicks) != EditStatus::Ok)
                    continue;
                candidates[k].accepted = true;
                candidates[k].event = event;
                const float jitter = static_cast<float>((e >> 40) * (1.0 / 16777216.0));
                candidates[k].threshold = 0.25f + 0.75f * (static_cast<float>(k) + jitter) / static_cast<float>(candidatesPerOperation);
            }
            if (!out.operations[op].enabled)
                continue; // a disabled operation gets no events at this chaos
            for (const auto& candidate : candidates)
                if (candidate.accepted && chaos >= candidate.threshold && result.count < maxLaneEvents)
                    result.events[result.count++] = candidate.event;
        }
        out.control.lane = result;
    }

    scene::sanitize(out);
    // Belt and braces: by construction nothing locked was changed; this restores anything that slipped.
    (void) enforcePermissions(scene, out);
    return out;
}
} // namespace groovemorph::core::control
