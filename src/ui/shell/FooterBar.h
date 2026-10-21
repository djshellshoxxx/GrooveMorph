#pragma once

#include <JuceHeader.h>

#include "ui/ControlMenu.h"

class GrooveMorphAudioProcessor;

namespace groovemorph::ui::shell
{
// Bottom line: hint for the hovered control, the MIDI note of the edited scene, return-on-note-off and MIDI status.
class FooterBar final : public juce::Component
{
public:
    static constexpr int height = 24;

    explicit FooterBar(GrooveMorphAudioProcessor& processor);
    ~FooterBar() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // Shows `text`, or the default hint when empty. Cheap when unchanged.
    void setHint(const juce::String& text);

    juce::Label hintLabel, midiLabel;
    groovemorph::ui::MenuSlider midiNoteSlider;
    groovemorph::ui::MenuToggleButton returnOnNoteOff{"Return on MIDI note-off"};

private:
    juce::Rectangle<int> noteCaption;
    juce::String currentHint;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FooterBar)
};
} // namespace groovemorph::ui::shell
