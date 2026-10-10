#pragma once

#include <JuceHeader.h>

class GrooveMorphAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit GrooveMorphAudioProcessorEditor(GrooveMorphAudioProcessor& p)
        : AudioProcessorEditor(&p), processor(p)
    {
        setSize(420, 180);

        titleLabel.setText("GrooveMorph", juce::dontSendNotification);
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));

        intensitySlider.setRange(0.0, 1.0, 0.01);
        intensitySlider.setValue(processor.getParameters()[0]->getValue());
        intensitySlider.onValueChange = [this]
        {
            processor.getParameters()[0]->setValueNotifyingHost(intensitySlider.getValue());
        };

        addAndMakeVisible(titleLabel);
        addAndMakeVisible(intensitySlider);
        addAndMakeVisible(valueLabel);

        updateValueLabel();
    }

    ~GrooveMorphAudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        g.drawText("M0 JUCE spike: bounded dry gain prototype", getLocalBounds(), juce::Justification::centredTop, false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(20);
        titleLabel.setBounds(area.removeFromTop(40));
        intensitySlider.setBounds(area.removeFromTop(40).withTrimmedLeft(20).withTrimmedRight(20));
        valueLabel.setBounds(area.removeFromTop(25));
    }

private:
    void updateValueLabel()
    {
        valueLabel.setText("Master Intensity: " + juce::String(intensitySlider.getValue(), 2), juce::dontSendNotification);
        valueLabel.setJustificationType(juce::Justification::centred);
    }

    GrooveMorphAudioProcessor& processor;
    juce::Label titleLabel;
    juce::Slider intensitySlider;
    juce::Label valueLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMorphAudioProcessorEditor)
};

#include "PluginProcessor.h"

juce::AudioProcessorEditor* GrooveMorphAudioProcessor::createEditor()
{
    return new GrooveMorphAudioProcessorEditor(*this);
}
