#pragma once

#include "core/control/SceneControl.h"

#include <cstddef>
#include <cstdint>

// N03 per-event / per-lane chance. Pure, constexpr, allocation-free and stateless, so it is safe on the
// audio thread and gives the same answer for any block size or evaluation order.
namespace groovemorph::core::control
{
namespace chance
{
inline constexpr unsigned fullScale = 10000; // probability resolution: 1/10000

// splitmix64 finalizer.
[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t x) noexcept
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Hash of (seed, operation, event identity, cycle index). Each input is mixed in sequence so that a change
// of any single one changes the result.
[[nodiscard]] constexpr std::uint64_t hash(std::uint32_t seed, std::uint32_t operation,
                                           std::int64_t eventId, std::int64_t cycle) noexcept
{
    std::uint64_t h = mix64(static_cast<std::uint64_t>(seed) ^ 0x4e303343484e4345ULL); // "N03CHNCE"
    h = mix64(h ^ (static_cast<std::uint64_t>(operation) + 0x100000000ULL));
    h = mix64(h ^ static_cast<std::uint64_t>(eventId));
    h = mix64(h ^ static_cast<std::uint64_t>(cycle));
    return h;
}

// Probability in 1/10000 for an event: event percent x lane percent (each clamped to 0..100).
[[nodiscard]] constexpr unsigned probability(unsigned eventPercent, unsigned lanePercent) noexcept
{
    return (eventPercent > 100u ? 100u : eventPercent) * (lanePercent > 100u ? 100u : lanePercent);
}

[[nodiscard]] constexpr bool passes(std::uint64_t h, unsigned probabilityScaled) noexcept
{
    if (probabilityScaled >= fullScale)
        return true;
    if (probabilityScaled == 0u)
        return false;
    return static_cast<unsigned>((h >> 32) % fullScale) < probabilityScaled;
}
} // namespace chance

// True when the event fires in lane cycle `cycle`. 100 % x 100 % never hashes.
[[nodiscard]] constexpr bool eventFires(const EventLane& lane, const LaneEvent& e, std::int64_t cycle) noexcept
{
    const unsigned p = chance::probability(e.chance, lane.chanceByOp[e.operation % laneCount]);
    if (p >= chance::fullScale)
        return true;
    return chance::passes(chance::hash(lane.chanceSeed, e.operation, e.onsetTicks, cycle), p);
}

// Gives a scene its own dice the first time chance is used (message thread). Deterministic in the scene
// index, never 0. Returns true when the seed was assigned.
inline bool ensureChanceSeed(EventLane& lane, std::size_t sceneIndex) noexcept
{
    if (lane.chanceSeed != 0)
        return false;
    const auto seed = static_cast<std::uint32_t>(chance::mix64(0x5343454e45ULL + sceneIndex) >> 32);
    lane.chanceSeed = seed != 0 ? seed : 1u;
    return true;
}
} // namespace groovemorph::core::control
