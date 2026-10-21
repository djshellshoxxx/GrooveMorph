#pragma once

#include <JuceHeader.h>

#include "core/viz/Palette.h"

// Single source of truth for the redesigned GUI (docs/specs/UI-REDESIGN.md section 3).
// Every colour, size and spacing used by new or restyled UI code comes from here. No raw 0xff hex in UI files.
// Effect colours are the tested Okabe-Ito set in core/viz/Palette.h; effect index order is the processor order:
// 0 Repeat, 1 Reverse, 2 Gate, 3 Half-time, 4 Tape stop, 5 Filter sweep, 6 Bit crush.
namespace groovemorph::ui::theme
{
namespace pal = groovemorph::core::viz::palette;

// Surfaces, darkest to lightest (about 4 to 6 percent lightness steps).
inline const juce::Colour bg{pal::slate};                // window
inline const juce::Colour surface{pal::panel};           // panels, tab content
inline const juce::Colour surfaceRaised{0xff252a35};     // cards, buttons, inputs
inline const juce::Colour surfaceHover{0xff2f3542};      // hover
inline const juce::Colour stroke{pal::line};             // dividers, outlines
inline const juce::Colour strokeStrong{0xff4a4f5e};      // focus-less emphasis outlines

// Text. text on bg/surface is >= 12:1; textDim >= 4.5:1 on surface (checked by PaletteTests).
inline const juce::Colour text{pal::ink};
inline const juce::Colour textDim{pal::dim};
inline const juce::Colour textOnAccent{0xff10131a};

// One accent for selected/active state, the hero knob arc and the playhead.
inline const juce::Colour accent{0xff4fd1c5};
inline const juce::Colour accentDim{0xff2a7f78};
inline const juce::Colour playhead{0xffffffff};

// Semantic states. Never the only cue: pair with text or icon.
inline const juce::Colour sounding{0xff2e9e6a};
inline const juce::Colour queued{0xffe6a700};
inline const juce::Colour warning{pal::warning};
inline const juce::Colour danger{0xffe5604f};
inline const juce::Colour focusRing{0xff8fd8ff};

inline constexpr int effectCount = 7;
inline juce::Colour effectColour(int effectIndex)
{
    static const std::uint32_t table[effectCount] = {pal::repeat, pal::reverse, pal::gate, pal::halfTime,
                                                     pal::tapeStop, pal::filterSweep, pal::bitCrush};
    return juce::Colour(table[juce::jlimit(0, effectCount - 1, effectIndex)]);
}
inline const char* effectName(int effectIndex)
{
    static const char* const names[effectCount] = {"Repeat", "Reverse", "Gate", "Half-time", "Tape stop", "Filter sweep", "Bit crush"};
    return names[juce::jlimit(0, effectCount - 1, effectIndex)];
}

// Spacing: multiples of 4. gap between panels 12, inside panels 8.
inline constexpr int space1 = 4, space2 = 8, space3 = 12, space4 = 16, space5 = 24;
inline constexpr float radius = 6.0f, radiusLarge = 10.0f;
inline constexpr int minTarget = 32;      // smallest clickable control
inline constexpr int padTarget = 56;      // scene pads, big buttons

// Type scale (logical px). Never below 11. Values 13+, labels 12, captions 11.
inline constexpr float fontCaption = 11.0f, fontLabel = 12.0f, fontBody = 13.0f, fontValue = 14.0f,
                       fontTitle = 16.0f, fontWordmark = 20.0f;
inline juce::Font font(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

// Window geometry (logical px). Layout is proportional to getLocalBounds(); these are only defaults/limits.
inline constexpr int defaultWidth = 1180, defaultHeight = 760;
inline constexpr int minWidth = 960, minHeight = 640;
inline constexpr int maxWidth = 2200, maxHeight = 1400;
} // namespace groovemorph::ui::theme
