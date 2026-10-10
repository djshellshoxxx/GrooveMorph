#pragma once

#include <JuceHeader.h>

class GrooveMorphAudioProcessor;

class GrooveMorphAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit GrooveMorphAudioProcessorEditor(GrooveMorphAudioProcessor& processor);
    ~GrooveMorphAudioProcessorEditor() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    GrooveMorphAudioProcessor& processor;
    juce::Label titleLabel;
    juce::Slider intensitySlider;
    juce::Label valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> intensityAttachment;

    void updateValueLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMorphAudioProcessorEditor)
};
