#pragma once

#include <JuceHeader.h>

#include "ui/ControlMenu.h"

#include <functional>
#include <memory>
#include <vector>

class GrooveMorphAudioProcessor;

namespace groovemorph::ui::shell
{
// The "Timing..." popover content: manual tempo, beats per bar, beat unit, timing method and phrase length.
// Same parameters, names and MIDI link targets the old main column had. The editor shows it as an overlay.
class TimingPanel final : public juce::Component
{
public:
    static constexpr int preferredWidth = 440, preferredHeight = 268;

    explicit TimingPanel(GrooveMorphAudioProcessor& processor);
    ~TimingPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    std::function<void()> onClose;

    groovemorph::ui::MenuSlider tempoSlider, meterSlider, phraseSlider;
    groovemorph::ui::MenuComboBox timingMode, denominator;
    juce::TextButton closeButton{"Close"};

private:
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimingPanel)
};
} // namespace groovemorph::ui::shell
