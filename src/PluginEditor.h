#pragma once

#include <JuceHeader.h>

class GrooveMorphAudioProcessor;

class GrooveMorphAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit GrooveMorphAudioProcessorEditor(GrooveMorphAudioProcessor& processor);
    ~GrooveMorphAudioProcessorEditor() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    GrooveMorphAudioProcessor& audioProcessor;
    juce::Label titleLabel;
    juce::Slider intensitySlider;
    juce::Label valueLabel;
    juce::Label timingLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> intensityAttachment;

    void updateValueLabel();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMorphAudioProcessorEditor)
};
