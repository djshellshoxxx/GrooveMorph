#include "core/dsp/WaveformPeaks.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core
{
int computePeaks(const float* const* channels, int numChannels, std::int64_t numSamples, int columns,
                 std::vector<float>& minimums, std::vector<float>& maximums)
{
    minimums.clear();
    maximums.clear();
    if (channels == nullptr || numChannels < 1 || numSamples < 1 || columns < 1)
        return 0;
    for (int c = 0; c < numChannels; ++c)
        if (channels[c] == nullptr)
            return 0;
    const auto count = static_cast<int>(std::min<std::int64_t>(columns, numSamples));
    minimums.assign(static_cast<std::size_t>(count), 0.0f);
    maximums.assign(static_cast<std::size_t>(count), 0.0f);
    for (int j = 0; j < count; ++j)
    {
        const std::int64_t begin = numSamples * j / count;
        const std::int64_t end = numSamples * (j + 1) / count;
        float lo = 0.0f;
        float hi = 0.0f;
        bool first = true;
        for (int c = 0; c < numChannels; ++c)
            for (std::int64_t i = begin; i < end; ++i)
            {
                const float v = std::isfinite(channels[c][i]) ? channels[c][i] : 0.0f;
                if (first)
                {
                    lo = hi = v;
                    first = false;
                }
                else
                {
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            }
        minimums[static_cast<std::size_t>(j)] = lo;
        maximums[static_cast<std::size_t>(j)] = hi;
    }
    return count;
}

std::vector<GridLine> computeBeatGrid(double loopBeats, double originBeats, int beatsPerBar)
{
    std::vector<GridLine> lines;
    if (!(loopBeats > 0.0) || !std::isfinite(loopBeats) || !std::isfinite(originBeats) || beatsPerBar < 1
        || std::abs(originBeats) > 1.0e9 || loopBeats > 1.0e6)
        return lines;
    const bool thin = loopBeats > 2048.0; // too fine: bar lines only
    const double first = std::ceil(originBeats - 1.0e-9);
    for (double beat = first; beat < originBeats + loopBeats - 1.0e-9; beat += 1.0)
    {
        const double whole = std::round(beat);
        const bool bar = std::fmod(std::fmod(whole, beatsPerBar) + beatsPerBar, beatsPerBar) < 0.5;
        if (thin && !bar)
            continue;
        if (lines.size() >= 2048)
            break;
        lines.push_back({(beat - originBeats) / loopBeats, bar});
    }
    return lines;
}

double loopPlayFraction(double timelineBeat, double originBeats, double loopBeats) noexcept
{
    if (!(loopBeats > 0.0) || !std::isfinite(loopBeats) || !std::isfinite(timelineBeat) || !std::isfinite(originBeats))
        return 0.0;
    double phase = std::fmod(timelineBeat - originBeats, loopBeats);
    if (phase < 0.0)
        phase += loopBeats;
    const double f = phase / loopBeats;
    return f >= 1.0 ? 0.0 : f;
}
} // namespace groovemorph::core
