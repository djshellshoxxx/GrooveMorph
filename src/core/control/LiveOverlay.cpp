#include "core/control/LiveOverlay.h"

namespace groovemorph::core::control
{
void applyLiveOverlays(VariationSettings& scene, const OperationTrims* trims, const ChaosContext* chaos) noexcept
{
    if (chaos != nullptr && chaos->chaos > 0.0f)
        scene = applyChaos(scene, *chaos);
    if (trims != nullptr && !isIdentity(*trims))
        applyTrims(scene, *trims);
}
} // namespace groovemorph::core::control
