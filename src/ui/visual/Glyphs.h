#pragma once

#include "ui/visual/VisualStyle.h"

namespace groovemorph::ui::visual
{
// One vector glyph per effect so colour is never the only cue (docs/visual-feedback/SPEC.md section 11).
void drawGlyph(juce::Graphics& g, groovemorph::core::viz::Effect effect, juce::Rectangle<float> area, juce::Colour colour,
               float lineWidth = 2.2f);
} // namespace groovemorph::ui::visual
