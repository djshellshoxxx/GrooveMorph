#include "PluginProcessor.h"

class GrooveMorphAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit GrooveMorphAudioProcessorEditor(GrooveMorphAudioProcessor& p);
    ~GrooveMorphAudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    GrooveMorphAudioProcessor& processor;

    juce::Label titleLabel;
    juce::Slider intensitySlider;
    juce::Label valueLabel;

    void updateValueLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMorphAudioProcessorEditor)
};
