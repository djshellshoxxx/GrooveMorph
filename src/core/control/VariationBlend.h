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
// W3: placement, regions, activity limits and lane events of BOTH scenes are blended too (docs/specs/W3-MORPH.md):
// discrete fields from `a` while weight < 0.5 and from `b` afterwards, events matched by (operation, onset, duration)
// interpolate, unmatched ones scale by their side's weight, same-operation overlap is resolved for the dominant side,
// at most maxLaneEvents result. `phraseTicks` (0 = unknown) sizes the full-phrase events a legacy-uniform side contributes.
// Scenes whose control state is identical keep the pre-W3 result exactly. Locks never apply. Phase and permissions come from `a`.
[[nodiscard]] VariationSettings morph(const VariationSettings& a, const VariationSettings& b,
                                      float weight, std::int64_t phraseTicks = 0) noexcept;

// C03: restore every locked field of `candidate` from `source` (post-hoc, so generator
// randomness is never perturbed by locks). Returns the number of fields restored.
// keepCandidatePermissions = true is for manual edits: the lock configuration being edited is kept
// while every other locked field is restored (the lock state used is the source's).
// phraseTicks (optional): when > 0, protected-event overlap is judged on the circular phrase, else linearly.
std::uint32_t enforcePermissions(const VariationSettings& source, VariationSettings& candidate,
                                 bool keepCandidatePermissions = false, std::int64_t phraseTicks = 0) noexcept;
} // namespace groovemorph::core::control
