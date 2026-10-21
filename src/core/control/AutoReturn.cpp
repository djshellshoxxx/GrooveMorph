#include "core/control/AutoReturn.h"

#include <cmath>
#include <cstdint>

namespace groovemorph::core::control
{
void AutoReturn::arm(double positionInBarsAtSwitch, int bars, std::size_t returnScene) noexcept
{
    if (bars < 1 || !std::isfinite(positionInBarsAtSwitch))
    {
        disarm();
        return;
    }
    armBar = std::floor(positionInBarsAtSwitch);
    targetBar = armBar + static_cast<double>(bars);
    scene = returnScene;
    isArmed = true;
    requested = false;
    dry = false;
}

void AutoReturn::armDry(double positionInBarsAtSwitch, int bars) noexcept
{
    arm(positionInBarsAtSwitch, bars, 0);
    dry = isArmed;
}

std::optional<std::int64_t> AutoReturn::dryBoundaryOffset(double positionInBars, double samplesPerBar) const noexcept
{
    if (!isArmed || !dry || !std::isfinite(positionInBars) || !std::isfinite(samplesPerBar) || !(samplesPerBar > 0.0))
        return std::nullopt;
    const double remaining = (targetBar - positionInBars) * samplesPerBar;
    if (remaining <= 1.0e-6)
        return 0;
    if (remaining > 4.0e18)
        return std::nullopt;
    return static_cast<std::int64_t>(std::ceil(remaining - 1.0e-6));
}

std::optional<std::size_t> AutoReturn::target() const noexcept
{
    return isArmed && !dry ? std::optional<std::size_t>(scene) : std::nullopt;
}

std::optional<std::size_t> AutoReturn::poll(double positionInBars) noexcept
{
    if (!isArmed || dry || requested || !std::isfinite(positionInBars))
        return std::nullopt;
    if (positionInBars < armBar - 1.0)
    {
        disarm(); // seek or loop wrapped before the arming point
        return std::nullopt;
    }
    // Strictly after the start of the bar before the target: a NextBar request made exactly on a bar
    // line would fire immediately (one bar early) instead of at the next boundary.
    if (positionInBars > targetBar - 1.0 + 1.0e-9)
    {
        requested = true;
        return scene;
    }
    return std::nullopt;
}
} // namespace groovemorph::core::control
