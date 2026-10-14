#include "core/viz/PlanBuilder.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::viz
{
std::size_t buildPlanSegments(const VariationSettings& scene, const control::GridContext& grid, double fromBeat,
                              double toBeat, PlanSegment* out, std::size_t capacity, double phaseShiftBeats) noexcept
{
    if (out == nullptr || capacity == 0 || !std::isfinite(fromBeat) || !std::isfinite(toBeat) || !(toBeat > fromBeat))
        return 0;

    std::uint32_t enabledMask = 0;
    for (std::size_t i = 0; i < operationCount; ++i)
        if (scene.operations[i].enabled)
            enabledMask |= 1u << i;

    if (!control::isActive(scene.control))
    {
        out[0] = PlanSegment{fromBeat, toBeat, scene, {}};
        return 1;
    }

    constexpr double minStepBeats = 1.0 / control::ticksPerBeat;
    std::size_t n = 0;
    double beat = fromBeat;
    while (beat < toBeat && n < capacity)
    {
        const auto targets = control::evaluate(scene.control, grid, beat, enabledMask,
                                                   std::isfinite(phaseShiftBeats) ? beat - phaseShiftBeats : beat);
        PlanSegment seg;
        seg.startBeat = beat;
        seg.settings = scene;
        if (targets.gated)
        {
            for (std::size_t i = 0; i < operationCount; ++i)
            {
                if (!targets.active[i])
                {
                    seg.settings.operations[i].enabled = false;
                }
                else
                {
                    seg.settings.operations[i].depth *= targets.depthScale[i];
                    if (targets.sliceBeats[i] > 0.0f)
                        seg.settings.operations[i].sliceBeats = targets.sliceBeats[i];
                    seg.phaseOffsetBeats[i] = targets.phaseOffsetBeats[i];
                }
            }
        }
        const double span = std::isfinite(targets.ticksUntilChange)
                                ? std::max(minStepBeats, targets.ticksUntilChange / control::ticksPerBeat)
                                : toBeat - beat;
        seg.endBeat = std::min(toBeat, beat + span);
        out[n++] = seg;
        beat = seg.endBeat;
    }
    return n;
}
} // namespace groovemorph::core::viz
