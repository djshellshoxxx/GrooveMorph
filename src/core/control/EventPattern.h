#pragma once

#include "core/control/ControlTypes.h"

#include <cstdint>
#include <optional>

namespace groovemorph::core::control
{
// Result of validating a pattern off the audio thread (C01 preparePattern contract).
enum class PatternStatus : std::uint8_t
{
    Valid,
    Invalid,  // bad id/operation/range/duplicate id, or legacy pattern carrying events
    Capacity, // more than maxEvents
    Overlap   // two events of the same operation overlap on the circular phrase
};

// Ticks per meter beat for a meter denominator (power of two, 1..32). nullopt otherwise.
[[nodiscard]] std::optional<std::int64_t> meterBeatTicks(int denominator) noexcept;

// Phrase length in ticks. nullopt for non-positive/over-range inputs or bad denominator.
// Bars 1..32 and numerator 1..32; the longest phrase is well inside 64-bit range.
[[nodiscard]] std::optional<std::int64_t> phraseTicks(int phraseBars, int numerator, int denominator) noexcept;

// Grid unit in ticks for subdivisions per meter beat (1,2,3,4,6,8,12,16).
// nullopt when the subdivision is unsupported or does not divide the beat exactly.
[[nodiscard]] std::optional<std::int64_t> gridTicks(std::int64_t beatTicks, int subdivision) noexcept;

// Floor-style positive modulo; modulus must be > 0.
[[nodiscard]] std::int64_t positiveModulo(std::int64_t value, std::int64_t modulus) noexcept;

// Validates ids, ranges, capacity and same-operation circular overlap (touching is allowed).
// Disabled events still count for overlap so re-enabling can never create a conflict.
[[nodiscard]] PatternStatus validatePattern(const EventPattern& pattern, std::int64_t phraseLengthTicks) noexcept;
} // namespace groovemorph::core::control
