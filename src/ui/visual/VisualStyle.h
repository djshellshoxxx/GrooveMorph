#pragma once

#include <JuceHeader.h>

#include "core/viz/Palette.h"
#include "core/viz/TextThrottle.h"
#include "core/viz/WeaveLayout.h"
#include "ui/theme/Theme.h"

// Shared palette for the Visual window (docs/visual-feedback/SPEC.md section 11). Effect colours are
// Okabe-Ito; every effect is also distinguished by shape/texture so colour is never the only cue.
namespace groovemorph::ui::visual
{
namespace theme = groovemorph::ui::theme;
namespace palette
{
// The Visual window shares the main UI palette (Theme.h); these names stay so the views read the same.
inline const juce::Colour slate = theme::bg;
inline const juce::Colour panel = theme::surface;
inline const juce::Colour line = theme::stroke;
inline const juce::Colour ink = theme::text;
inline const juce::Colour dim = theme::textDim;
inline const juce::Colour hatch = theme::strokeStrong;
inline const juce::Colour warning = theme::warning;
} // namespace palette

// U7: the views are refreshed at 30 Hz, but screen readers only need a new description when the text
// changed, and not more than about 4 times per second.
inline void setDescriptionThrottled(juce::Component& component, groovemorph::core::viz::TextThrottle& throttle,
                                    const juce::String& text)
{
    if (throttle.apply(static_cast<std::int64_t>(juce::Time::getMillisecondCounter()), text.toStdString()))
        component.setDescription(text);
}

inline juce::Colour effectColour(groovemorph::core::viz::Effect e)
{
    using E = groovemorph::core::viz::Effect;
    switch (e)
    {
        case E::Repeat: return theme::effectColour(0);
        case E::Reverse: return theme::effectColour(1);
        case E::Gate: return theme::effectColour(2);
        case E::HalfTime: return theme::effectColour(3);
        case E::TapeStop: return theme::effectColour(4);
        case E::FilterSweep: return theme::effectColour(5);
        case E::BitCrush: return theme::effectColour(6);
        case E::Live: break;
    }
    return juce::Colour(groovemorph::core::viz::palette::live);
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
