#include "core/easy/FirstRunSteps.h"

#include <algorithm>
#include <array>

namespace groovemorph::core::easy
{
namespace
{
constexpr std::array<GuideStep, firstRunStepCount> steps{{
    {"1. Set the transport",
     "In your host, press play. In the standalone app the built-in clock runs on its own. "
     "The line under the title tells you what GrooveMorph is following."},
    {"2. Pick a scene",
     "A to D are four different rhythm looks. Click one. By default it takes over at the next bar, "
     "and the button shows when it is queued or sounding."},
    {"3. Press Surprise me",
     "Surprise me tries a new variation of the sounding scene and plays it right away. "
     "It is kept at the next phrase. Undo last change takes it back."},
    {"4. Open the full view",
     "Open full view shows every control: scene editing, the generator and the inspector. "
     "The Easy mode button there brings you back to this screen."},
}};
} // namespace

const GuideStep& firstRunStep(std::size_t index) noexcept
{
    return steps[std::min(index, firstRunStepCount - 1)];
}
} // namespace groovemorph::core::easy
