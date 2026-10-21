#pragma once

#include "core/OperationParams.h"
#include "core/control/ChaosMapper.h"
#include "core/control/OperationTrims.h"

// The one fixed order in which the live (never stored) overlays are applied to the SOUNDING scene,
// after the morph blend and before placement gating:
//   1. Chaos macro (N05): adds activity to what the scene says.
//   2. Operation trims (A06): the player's explicit per-operation overrides.
// Trims come last so that a trim set to Off stays off even at high Chaos, a trim set to On stays on, and
// the depth/slice multipliers scale the value that actually sounds. With chaos <= 0 (or NaN) and every
// trim at its default this is the identity, bit for bit. Real-time safe: noexcept, no allocation.
namespace groovemorph::core::control
{
void applyLiveOverlays(VariationSettings& scene, const OperationTrims* trims, const ChaosContext* chaos) noexcept;
} // namespace groovemorph::core::control
