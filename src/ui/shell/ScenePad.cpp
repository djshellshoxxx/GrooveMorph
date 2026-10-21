#include "ui/shell/ScenePad.h"

#include "ui/theme/Theme.h"

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

    auto inner = getLocalBounds().reduced(theme::space2, theme::space1 + 2);
    // Big letter on the left, shortcut hint under it.
    auto letterArea = inner.removeFromLeft(34);
    g.setColour(theme::text);
    g.setFont(theme::font(28.0f, true));
    g.drawText(letter, letterArea.removeFromTop(34), juce::Justification::centredLeft, false);
    g.setColour(theme::textDim);
    g.setFont(theme::font(theme::fontCaption));
    g.drawText("key " + keyHint, juce::Rectangle<int>(letterArea.getX(), getHeight() - 4 - 14, 44, 14),
               juce::Justification::bottomLeft, false);

    // State chip, then the name, then the editing marker.
    inner.removeFromLeft(theme::space1);
    auto chipRow = inner.removeFromTop(16);
    const char* stateText = state.queued ? "QUEUED" : state.sounding ? "SOUNDING" : nullptr;
    if (stateText != nullptr)
    {
        const auto chipColour = state.queued ? theme::queued : theme::sounding;
        const int chipWidth = juce::jmin(chipRow.getWidth(), state.queued ? 62 : 76);
        const auto chip = chipRow.removeFromLeft(chipWidth);
        g.setColour(chipColour);
        g.fillRoundedRectangle(chip.toFloat(), 3.0f);
        g.setColour(theme::textOnAccent);
        g.setFont(theme::font(theme::fontCaption, true));
        g.drawText(stateText, chip, juce::Justification::centred, false);
    }
    g.setColour(theme::text);
    g.setFont(theme::font(theme::fontBody));
    g.drawText(state.name, inner.removeFromTop(20), juce::Justification::centredLeft, true);
    if (state.editing)
    {
        g.setColour(theme::accent);
        g.setFont(theme::font(theme::fontCaption, true));
        g.drawText("EDITING", inner.removeFromTop(14), juce::Justification::centredLeft, false);
    }
    if (hasKeyboardFocus(false))
    {
        g.setColour(theme::focusRing);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), theme::radius, 2.0f);
    }
}
} // namespace groovemorph::ui::shell
