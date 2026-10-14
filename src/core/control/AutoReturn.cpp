#include "core/control/AutoReturn.h"

#include <cmath>

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
}

std::optional<std::size_t> AutoReturn::target() const noexcept
{
    return isArmed ? std::optional<std::size_t>(scene) : std::nullopt;
}

std::optional<std::size_t> AutoReturn::poll(double positionInBars) noexcept
{
    if (!isArmed || requested || !std::isfinite(positionInBars))
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
