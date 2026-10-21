#include "ui/shell/ScenePad.h"

#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::ui::shell
{
namespace theme = groovemorph::ui::theme;

ScenePad::ScenePad()
{
    setClickingTogglesState(false);
    setWantsKeyboardFocus(true);
    setIndex(0);
}

void ScenePad::setIndex(int sceneIndex)
{
    letter = juce::String::charToString(static_cast<juce::juce_wchar>('A' + sceneIndex));
    keyHint = juce::String(sceneIndex + 1);
    setTitle("Scene " + letter);
}

void ScenePad::setState(const State& newState)
{
    if (state == newState)
        return;
    state = newState;
    juce::String text = letter + ": " + state.name;
    if (state.sounding) text << " [sounding]";
    if (state.queued) text << " [queued]";
    if (state.editing) text << " [editing]";
    setButtonText(text);
    repaint();
}

void ScenePad::tick()
{
    if (state.queued)
        repaint();
}

void ScenePad::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    juce::Colour fill = highlighted || down ? theme::surfaceHover : theme::surfaceRaised;
    juce::Colour edge = theme::stroke;
    float edgeWidth = 1.0f;
    if (state.sounding)
    {
        fill = theme::sounding.withAlpha(0.30f);
        edge = theme::sounding;
        edgeWidth = 2.0f;
    }
    if (state.queued)
    {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(juce::Time::getMillisecondCounter() % 100000u) * 0.008f);
        fill = theme::queued.withAlpha(0.15f + 0.20f * pulse);
        edge = theme::queued;
        edgeWidth = 2.0f;
    }
    g.setColour(fill);
    g.fillRoundedRectangle(b, theme::radius);
    g.setColour(edge);
    g.drawRoundedRectangle(b.reduced(edgeWidth * 0.5f), theme::radius, edgeWidth);

    // Rows (pad is 56 px high): state chip across the top, then big letter on the left with the name beside it,
    // then "key N" under the letter and the EDITING marker beside it. The chip spans the pad so
    // "SOUNDING" is never clipped, even on the narrow pads at the 960 px window width.
    const int w = getWidth(), h = getHeight();
    const int pad = theme::space2;
    const char* stateText = state.queued ? "QUEUED" : state.sounding ? "SOUNDING" : nullptr;
    if (stateText != nullptr)
    {
        const auto chipColour = state.queued ? theme::queued : theme::sounding;
        const auto chip = juce::Rectangle<int>(pad, 5, std::min(w - 2 * pad, state.queued ? 62 : 76), 15);
        g.setColour(chipColour);
        g.fillRoundedRectangle(chip.toFloat(), 3.0f);
        g.setColour(theme::textOnAccent);
        g.setFont(theme::font(theme::fontCaption, true));
        g.drawFittedText(stateText, chip, juce::Justification::centred, 1, 0.8f);
    }
    g.setColour(theme::text); // text (not textDim) keeps >= 4.5:1 on the sounding and queued tints
    g.setFont(theme::font(26.0f, true));
    g.drawText(letter, juce::Rectangle<int>(pad, 19, 30, 22), juce::Justification::centredLeft, false);
    const int textX = pad + 32;
    g.setFont(theme::font(theme::fontBody));
    g.drawText(state.name, juce::Rectangle<int>(textX, 21, std::max(0, w - textX - 4), 18), juce::Justification::centredLeft, true);
    g.setFont(theme::font(theme::fontCaption));
    g.drawText("key " + keyHint, juce::Rectangle<int>(pad, h - 15, 32, 12), juce::Justification::centredLeft, false);
    if (state.editing)
    {
        g.setFont(theme::font(theme::fontCaption, true));
        g.drawFittedText("EDITING", juce::Rectangle<int>(textX, h - 15, std::max(0, w - textX - 4), 12),
                         juce::Justification::centredLeft, 1, 0.8f);
    }
    if (hasKeyboardFocus(false))
    {
        g.setColour(theme::focusRing);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), theme::radius, 2.0f);
    }
}
} // namespace groovemorph::ui::shell
