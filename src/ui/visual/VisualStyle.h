#pragma once

#include <JuceHeader.h>

#include "core/viz/Palette.h"
#include "core/viz/WeaveLayout.h"

// Shared palette for the Visual window (docs/visual-feedback/SPEC.md section 11). Effect colours are
// Okabe-Ito; every effect is also distinguished by shape/texture so colour is never the only cue.
namespace groovemorph::ui::visual
{
namespace palette
{
namespace shared = groovemorph::core::viz::palette;
inline const juce::Colour slate{shared::slate};
inline const juce::Colour panel{shared::panel};
inline const juce::Colour line{shared::line};
inline const juce::Colour ink{shared::ink};
inline const juce::Colour dim{shared::dim};
inline const juce::Colour hatch{shared::hatch};
inline const juce::Colour warning{shared::warning};
} // namespace palette

inline juce::Colour effectColour(groovemorph::core::viz::Effect e)
{
    using E = groovemorph::core::viz::Effect;
    switch (e)
    {
        case E::Repeat: return juce::Colour(palette::shared::repeat);
        case E::Reverse: return juce::Colour(palette::shared::reverse);
        case E::Gate: return juce::Colour(palette::shared::gate);
        case E::HalfTime: return juce::Colour(palette::shared::halfTime);
        case E::TapeStop: return juce::Colour(palette::shared::tapeStop);
        case E::FilterSweep: return juce::Colour(palette::shared::filterSweep);
        case E::BitCrush: return juce::Colour(palette::shared::bitCrush);
        case E::Live: break;
    }
    return juce::Colour(palette::shared::live);
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
        case E::FilterSweep: return "filter-sweep";
        case E::BitCrush: return "bit-crush";
        case E::Live: break;
    }
    return "live";
}
} // namespace groovemorph::ui::visual
