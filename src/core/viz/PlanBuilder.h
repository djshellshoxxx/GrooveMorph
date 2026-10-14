#pragma once

#include "core/control/PlacementEvaluator.h"
#include "core/viz/PlannedPath.h"

#include <cstddef>

namespace groovemorph::core::viz
{
// Splits [fromBeat, toBeat) into segments of constant effective settings, exactly as the processor's
// render loop does: placement windows, event lanes and activity limits gate and scale operations, event
// slices override the operation slice, restart-cycle events set a phase anchor, and each segment ends at
// the next placement boundary (docs/visual-feedback/SPEC.md section 6). Returns the segment count; when
// `capacity` runs out before toBeat the remaining range is left uncovered (the planner then reports it
// as invalid rather than guessing). Allocation-free.
[[nodiscard]] std::size_t buildPlanSegments(const VariationSettings& scene, const control::GridContext& grid,
                                            double fromBeat, double toBeat, PlanSegment* out,
                                            std::size_t capacity) noexcept;
} // namespace groovemorph::core::viz
