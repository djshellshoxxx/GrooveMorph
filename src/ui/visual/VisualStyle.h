#pragma once

#include <JuceHeader.h>

#include "core/viz/WeaveLayout.h"

// Shared palette for the Visual window (docs/visual-feedback/SPEC.md section 11). Effect colours are
// Okabe-Ito; every effect is also distinguished by shape/texture so colour is never the only cue.
namespace groovemorph::ui::visual
{
namespace palette
{
inline const juce::Colour slate{0xff15171d};
inline const juce::Colour panel{0xff1c1f27};
inline const juce::Colour line{0xff2c303b};
inline const juce::Colour ink{0xffece6d6};
inline const juce::Colour dim{0xff8b90a0};
inline const juce::Colour hatch{0xff4a4f5e};
inline const juce::Colour warning{0xffe69f00};
} // namespace palette

inline juce::Colour effectColour(groovemorph::core::viz::Effect e)
{
    using E = groovemorph::core::viz::Effect;
    switch (e)
    {
        case E::Repeat: return juce::Colour(0xff56b4e9);
        case E::Reverse: return juce::Colour(0xffe69f00);
        case E::Gate: return juce::Colour(0xffcc79a7);
        case E::HalfTime: return juce::Colour(0xff009e73);
        case E::TapeStop: return juce::Colour(0xfff0e442);
        case E::Live: break;
    }
    return juce::Colour(0xffb9b4a6);
}

inline const char* effectName(groovemorph::core::viz::Effect e)
{
    using E = groovemorph::core::viz::Effect;
    switch (e)
    {
        case E::Repeat: return "repeat";
        case E::Reverse: return "reverse";
        case E::Gate: return "gate";
        case E::HalfTime: return "half-time";
        case E::TapeStop: return "tape-stop";
        case E::Live: break;
    }
    return "live";
}
} // namespace groovemorph::ui::visual
