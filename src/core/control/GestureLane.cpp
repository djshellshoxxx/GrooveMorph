#include "core/control/GestureLane.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::control
{
namespace
{
int binOf(double fraction) noexcept
{
    if (!std::isfinite(fraction))
        return 0;
    double f = fraction - std::floor(fraction);
    return std::clamp(static_cast<int>(f * GestureLane::binCount), 0, GestureLane::binCount - 1);
}
} // namespace

void GestureLane::clear() noexcept
{
    values.fill(0.0f);
    has.fill(false);
    filled = 0;
}

void GestureLane::record(double phraseFraction, float value) noexcept
{
    const int bin = binOf(phraseFraction);
    if (!has[static_cast<std::size_t>(bin)])
    {
        has[static_cast<std::size_t>(bin)] = true;
        ++filled;
    }
    values[static_cast<std::size_t>(bin)] = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float GestureLane::sample(double phraseFraction) const noexcept
{
    if (filled == 0)
        return -1.0f;
    const int bin = binOf(phraseFraction);
    // Previous and next recorded bins (wrapping), interpolated by distance.
    int prev = -1, next = -1, dPrev = 0, dNext = 0;
    for (int d = 0; d < binCount && prev < 0; ++d)
    {
        const int b = (bin - d + binCount) % binCount;
        if (has[static_cast<std::size_t>(b)]) { prev = b; dPrev = d; }
    }
    for (int d = 1; d <= binCount && next < 0; ++d)
    {
        const int b = (bin + d) % binCount;
        if (has[static_cast<std::size_t>(b)]) { next = b; dNext = d; }
    }
    if (prev < 0 || next < 0 || dPrev == 0)
        return values[static_cast<std::size_t>(prev < 0 ? next : prev)];
    const float a = values[static_cast<std::size_t>(prev)], b = values[static_cast<std::size_t>(next)];
    const float t = static_cast<float>(dPrev) / static_cast<float>(dPrev + dNext);
    return a + (b - a) * t;
}
} // namespace groovemorph::core::control
