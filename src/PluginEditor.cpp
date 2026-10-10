#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
constexpr auto masterIntensityId = "masterIntensity";
}

GrooveMorphAudioProcessorEditor::GrooveMorphAudioProcessorEditor(
    GrooveMorphAudioProcessor& processorToEdit)
    : AudioProcessorEditor(&processorToEdit), processor(processorToEdit)
{
    setSize(420, 180);
    setResizable(true, true);
    setResizeLimits(320, 140, 900, 500);

    titleLabel.setText("GrooveMorph", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));

    intensitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    intensitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 24);
    intensitySlider.setRange(0.0, 1.0, 0.01);
    intensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, masterIntensityId, intensitySlider);
    intensitySlider.onValueChange = [this] { updateValueLabel(); };

    valueLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(intensitySlider);
    addAndMakeVisible(valueLabel);
    updateValueLabel();
}

void GrooveMorphAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
    graphics.setColour(juce::Colours::white);
    graphics.drawText("M0 spike: parameterized dry-gain path", getLocalBounds(),
                      juce::Justification::centredTop, false);
}

void GrooveMorphAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    titleLabel.setBounds(area.removeFromTop(40));
    intensitySlider.setBounds(area.removeFromTop(42).withTrimmedLeft(12).withTrimmedRight(12));
    valueLabel.setBounds(area.removeFromTop(26));
}

void GrooveMorphAudioProcessorEditor::updateValueLabel()
{
    valueLabel.setText("Master Intensity: " + juce::String(intensitySlider.getValue(), 2),
                       juce::dontSendNotification);
}

juce::AudioProcessorEditor* GrooveMorphAudioProcessor::createEditor()
{
    return new GrooveMorphAudioProcessorEditor(*this);
}
