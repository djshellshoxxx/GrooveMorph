#include "core/dsp/LoopImport.h"

#include <cmath>

namespace groovemorph::core
{
LoopMapping mapLoop(double seconds, int beatsPerBar, double currentBpm, int bars, double nativeBpm) noexcept
{
    LoopMapping m;
    if (!(seconds > 0.0) || !std::isfinite(seconds) || beatsPerBar < 1 || beatsPerBar > 64)
        return m;
    if (nativeBpm > 0.0 && std::isfinite(nativeBpm))
    {
        m.beats = seconds * nativeBpm / 60.0;
        m.fromBpm = true;
    }
    else if (bars > 0)
    {
        m.beats = static_cast<double>(bars) * beatsPerBar;
    }
    else
    {
        static constexpr int candidates[] = {1, 2, 4, 8, 16};
        const double target = currentBpm > 0.0 && std::isfinite(currentBpm) ? currentBpm : 120.0;
        double best = 1.0e300;
        int bestBars = 0;
        bool bestInRange = false;
        for (const int b : candidates)
        {
            const double bpm = static_cast<double>(b) * beatsPerBar * 60.0 / seconds;
            const bool inRange = bpm >= 60.0 && bpm <= 200.0;
            const double distance = std::abs(std::log(bpm / target));
            if ((inRange && !bestInRange) || (inRange == bestInRange && distance < best))
            {
                best = distance;
                bestBars = b;
                bestInRange = inRange;
            }
        }
        m.beats = static_cast<double>(bestBars) * beatsPerBar;
    }
    if (!(m.beats > 0.0) || !std::isfinite(m.beats))
        return LoopMapping{};
    m.nativeBpm = m.beats * 60.0 / seconds;
    m.bars = m.beats / static_cast<double>(beatsPerBar);
    m.valid = std::isfinite(m.nativeBpm) && m.nativeBpm > 0.0;
    return m;
}
} // namespace groovemorph::core
