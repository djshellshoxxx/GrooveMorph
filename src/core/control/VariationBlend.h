#pragma once

#include "core/OperationParams.h"
#include "core/control/SceneControl.h"

#include <cstdint>

namespace groovemorph::core::control
{
// C02 variation distance: move `source` toward `candidate` by distance in [0,1].
// distance 0 returns the source effect settings exactly, 1 the candidate exactly.
// Depth interpolates linearly; slice interpolates in log2 (beat-length) space; an enabled
// flip is applied only when distance >= 0.5. Placement/lane/permission/activity state is
// taken from `source` unchanged.
[[nodiscard]] VariationSettings blendDistance(const VariationSettings& source,
                                              const VariationSettings& candidate,
                                              float distance) noexcept;

// F02 scene morph: weight 0 = a, 1 = b. A one-sided operation fades in/out with depth scaled
// by its side's weight; two-sided operations interpolate depth/slice. Control state from `a`.
[[nodiscard]] VariationSettings morph(const VariationSettings& a, const VariationSettings& b,
                                      float weight) noexcept;

// C03: restore every locked field of `candidate` from `source` (post-hoc, so generator
// randomness is never perturbed by locks). Returns the number of fields restored.
// keepCandidatePermissions = true is for manual edits: the lock configuration being edited is kept
// while every other locked field is restored (the lock state used is the source's).
// phraseTicks (optional): when > 0, protected-event overlap is judged on the circular phrase, else linearly.
std::uint32_t enforcePermissions(const VariationSettings& source, VariationSettings& candidate,
                                 bool keepCandidatePermissions = false, std::int64_t phraseTicks = 0) noexcept;
} // namespace groovemorph::core::control
