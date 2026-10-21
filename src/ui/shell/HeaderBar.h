#pragma once

#include <JuceHeader.h>

#include "ui/ControlMenu.h"
#include "ui/PresetBar.h"

class GrooveMorphAudioProcessor;

namespace groovemorph::ui::shell
{
// Text-less button drawn as a gear (the settings menu).
class GearButton final : public juce::TextButton
{
public:
    GearButton() : juce::TextButton("Settings") {}
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

// Top bar: wordmark, preset bar, transport, Easy / Visual / Bypass and the settings gear.
// The owner wires the callbacks; this component only lays out and paints.
class HeaderBar final : public juce::Component
{
public:
    static constexpr int height = 48;

    explicit HeaderBar(GrooveMorphAudioProcessor& processor);
    ~HeaderBar() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    juce::Label wordmark;
    groovemorph::ui::PresetBar presetBar;
    juce::TextButton playButton{"Play"}, stopButton{"Stop"}, easyButton{"Easy"};
    groovemorph::ui::MenuToggleButton visualButton{"Visual"}, bypassButton{"Bypass"};
    GearButton settingsButton;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderBar)
};
} // namespace groovemorph::ui::shell
