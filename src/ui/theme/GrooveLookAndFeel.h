#pragma once

#include <JuceHeader.h>

#include "ui/theme/Theme.h"

// Application-wide look (docs/specs/UI-REDESIGN.md section 3). Construct once, call
// juce::LookAndFeel::setDefaultLookAndFeel or component.setLookAndFeel(&laf) on the editor root.
namespace groovemorph::ui::theme
{
class GrooveLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    GrooveLookAndFeel();
    // Implemented by the look-and-feel worker: rotary/linear sliders, buttons, combo, popup menu, tabs,
    // scrollbar, tooltip, labels, text editor, alert window, fonts.
};
} // namespace groovemorph::ui::theme
