#include "core/easy/EasyText.h"

namespace groovemorph::core::easy
{
std::string describeEasyTiming(const EasyTimingInput& input)
{
    if (!input.prepared)
        return "Audio is not running yet";
    if (!input.valid)
        return "No usable timing yet. Press play in your host, or set a tempo in the full view";
    if (input.internalClock || input.source == TimingSource::FreeRun)
        return input.playing ? "Using sample clock" : "Using sample clock (stopped, press Play in the full view)";
    switch (input.source)
    {
        case TimingSource::Host:
            return input.playing ? "Following the host transport" : "Waiting for host transport";
        case TimingSource::Mixed:
            return input.playing ? "Following the host transport (some timing comes from the manual settings)"
                                 : "Waiting for host transport";
        case TimingSource::Manual:
            return input.playing ? "Using the manual tempo (the host sends no timing)" : "Waiting for host transport";
        case TimingSource::FreeRun:
            break;
    }
    return "Using sample clock";
}
} // namespace groovemorph::core::easy
