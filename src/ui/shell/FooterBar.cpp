#include "ui/shell/FooterBar.h"

#include "PluginProcessor.h"
#include "ui/shell/ShellUtil.h"

#include <algorithm>

namespace groovemorph::ui::shell
{
namespace theme = groovemorph::ui::theme;
namespace
{
const char* const defaultHint = "Hover a control to see what it does. F1 shows the keyboard shortcuts.";
}

FooterBar::FooterBar(GrooveMorphAudioProcessor& processor)
{
    hintLabel.setFont(theme::font(theme::fontLabel));
    hintLabel.setColour(juce::Label::textColourId, theme::textDim);
    hintLabel.setText(defaultHint, juce::dontSendNotification);
    hintLabel.setInterceptsMouseClicks(false, false);
    midiLabel.setFont(theme::font(theme::fontLabel));
    midiLabel.setColour(juce::Label::textColourId, theme::text);
    midiLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(hintLabel);
    addAndMakeVisible(midiLabel);

    configureLinear(midiNoteSlider, "MIDI note for edited variation (0-127)", 40, 20);
    midiNoteSlider.setRange(0, 127, 1);
    midiNoteSlider.setNumDecimalPlacesToDisplay(0);
    midiNoteSlider.info() = {&processor, "MIDI note for edited variation (0-127)", {}, {}};
    describe(midiNoteSlider, "MIDI note for edited variation",
             "MIDI note (0-127; 60 = middle C) that switches to the scene being edited. Each scene has its own note.");
    describe(returnOnNoteOff, "Return on MIDI note-off",
             "When on, letting go of a scene's MIDI note returns to the scene that was sounding before it (applies to all four scenes). When off, releasing a note does nothing.");
    midiNoteSlider.setWantsKeyboardFocus(true);
    returnOnNoteOff.setWantsKeyboardFocus(true);
    addAndMakeVisible(midiNoteSlider);
    addAndMakeVisible(returnOnNoteOff);
}

FooterBar::~FooterBar() = default;

void FooterBar::setHint(const juce::String& text)
{
    const auto shown = text.isEmpty() ? juce::String(defaultHint) : text;
    if (shown == currentHint)
        return;
    currentHint = shown;
    hintLabel.setText(shown, juce::dontSendNotification);
}

void FooterBar::paint(juce::Graphics& g)
{
    paintPanel(g, getLocalBounds());
    paintCaption(g, "MIDI note", noteCaption);
}

void FooterBar::resized()
{
    auto area = getLocalBounds().reduced(theme::space2, 2);
    const int gap = theme::space2;
    midiLabel.setBounds(area.removeFromRight(std::clamp(area.getWidth() / 3, 200, 420)));
    area.removeFromRight(gap);
    returnOnNoteOff.setBounds(area.removeFromRight(190));
    area.removeFromRight(gap);
    midiNoteSlider.setBounds(area.removeFromRight(190));
    noteCaption = area.removeFromRight(62);
    area.removeFromRight(gap);
    hintLabel.setBounds(area);
}
} // namespace groovemorph::ui::shell
