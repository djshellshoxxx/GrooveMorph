#pragma once

#include "core/control/SceneControl.h"

#include <cstdint>

// Conflict badge for event lanes after a meter or phrase-length change (known issue: events keep
// their tick positions, so events past a shortened phrase silently wrap around).
namespace groovemorph::core::control
{
struct PhraseFitReport
{
    std::uint8_t pastEnd = 0;   // events whose onset is at or after the phrase end (they wrap to the start)
    std::uint8_t overhang = 0;  // events that start inside but extend past the phrase end (they wrap)
    std::uint8_t lockedAffected = 0; // how many of the above are locked (the Scale command keeps them as they are)

    [[nodiscard]] bool conflict() const noexcept { return pastEnd != 0 || overhang != 0; }
    [[nodiscard]] int affected() const noexcept { return pastEnd + overhang; }
};

// Counts lane events that do not fit in a phrase of `phraseTicks`. Only explicit event lanes are
// checked (uniform mode has no events). Events of an operation with a polyrhythm loop length are
// judged against that loop ring, because that is where the audio side wraps them.
[[nodiscard]] PhraseFitReport checkPhraseFit(const EventLane& lane, std::int64_t phraseTicks,
                                             const PermissionConfig* permissions = nullptr) noexcept;
} // namespace groovemorph::core::control
