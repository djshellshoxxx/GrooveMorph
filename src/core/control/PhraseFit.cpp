#include "core/control/PhraseFit.h"

namespace groovemorph::core::control
{
PhraseFitReport checkPhraseFit(const EventLane& lane, std::int64_t phraseTicks,
                               const PermissionConfig* permissions) noexcept
{
    PhraseFitReport report;
    if (!lane.explicitEvents || phraseTicks <= 0)
        return report;
    for (std::size_t i = 0; i < lane.count && i < maxLaneEvents; ++i)
    {
        const auto& e = lane.events[i];
        const auto loop = lane.loopBeats[e.operation % laneCount];
        const std::int64_t ring = loop > 0 ? static_cast<std::int64_t>(loop) * ticksPerBeat : phraseTicks;
        const std::int64_t end = static_cast<std::int64_t>(e.onsetTicks) + e.durationTicks;
        bool affected = false;
        if (e.onsetTicks >= ring)
        {
            if (report.pastEnd < 255)
                ++report.pastEnd;
            affected = true;
        }
        else if (end > ring)
        {
            if (report.overhang < 255)
                ++report.overhang;
            affected = true;
        }
        if (affected)
        {
            const bool operationLocked = permissions != nullptr && e.operation < laneCount
                                         && (permissions->operationLocks[e.operation] & lock::events) != 0;
            if ((e.locked || operationLocked) && report.lockedAffected < 255)
                ++report.lockedAffected;
        }
    }
    return report;
}
} // namespace groovemorph::core::control
