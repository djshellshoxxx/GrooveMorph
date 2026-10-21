#pragma once

#include "core/OperationParams.h"

#include <cstdint>

// Undo granularity for repeated small edits (known issue: wheel/keyboard slider steps were one undo
// entry each, so 64 steps pushed everything else off the 64-deep stack).
namespace groovemorph::core::scene
{
// Window in which successive edits of the same single field merge into one undo entry.
inline constexpr std::int64_t undoCoalesceWindowMs = 500;

// Identifies the one numeric scene field (operation depth/slice, placement numbers, activity limits,
// phase offset) in which `after` differs from `before`. Returns 0 when nothing differs, when more than
// one field differs, or when the difference is not a numeric field (toggles, events, locks, regions).
// Only numeric fields are coalesced, so a toggle or a structural edit always gets its own undo entry.
[[nodiscard]] std::uint32_t singleChangedNumericField(const VariationSettings& before,
                                                      const VariationSettings& after) noexcept;

// Coalescing key for an edit of scene `sceneIndex`, or 0 (never merge).
[[nodiscard]] std::uint64_t coalesceKey(std::size_t sceneIndex, const VariationSettings& before,
                                        const VariationSettings& after) noexcept;
} // namespace groovemorph::core::scene
