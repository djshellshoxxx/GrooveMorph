#include "core/control/PhaseResolver.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::control
{
namespace
{
double positiveMod(double x, double m) noexcept
{
    const double r = std::fmod(x, m);
    return r < 0.0 ? r + m : r;
}

double phraseBeats(const GridContext& grid) noexcept
{
    return static_cast<double>(std::max(1, grid.beatsPerBar)) * static_cast<double>(std::max(1, grid.phraseBars));
}
} // namespace

bool sanitize(PhaseConfig& config) noexcept
{
    bool changed = false;
    if (static_cast<std::uint8_t>(config.policy) > static_cast<std::uint8_t>(PhasePolicy::AlignBeat))
    {
        config.policy = PhasePolicy::Preserve;
        changed = true;
    }
    float offset = config.offsetBeats;
    if (!std::isfinite(offset))
        offset = 0.0f;
    offset = std::clamp(offset, -4.0f, 4.0f);
    if (offset != config.offsetBeats)
    {
        config.offsetBeats = offset;
        changed = true;
    }
    return changed;
}

bool samePhase(const PhaseConfig& a, const PhaseConfig& b) noexcept
{
    return a.policy == b.policy && a.offsetBeats == b.offsetBeats;
}

double resolveLocalPhase(const PhaseConfig& config, double positionBeats, const GridContext& grid) noexcept
{
    const double P = phraseBeats(grid);
    const double q = std::isfinite(positionBeats) ? positionBeats : 0.0;
    double offset = static_cast<double>(config.offsetBeats);
    if (!std::isfinite(offset))
        offset = 0.0;
    offset = std::clamp(offset, -4.0, 4.0);
    switch (config.policy)
    {
        case PhasePolicy::Restart: return positiveMod(offset, P);
        case PhasePolicy::AlignBeat: return positiveMod(std::ceil(q - 1.0e-12) + offset, P);
        case PhasePolicy::Preserve:
        default: return positiveMod(q + offset, P);
    }
}

double resolvePhaseShift(const PhaseConfig& config, double positionBeats, const GridContext& grid) noexcept
{
    const double P = phraseBeats(grid);
    const double q = std::isfinite(positionBeats) ? positionBeats : 0.0;
    const double shift = positiveMod(resolveLocalPhase(config, q, grid) - q, P);
    // Preserve with a zero offset is an exact identity; avoid a 1-ulp residue from the modulo.
    return shift < 1.0e-9 || P - shift < 1.0e-9 ? 0.0 : shift;
}
} // namespace groovemorph::core::control
