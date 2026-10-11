#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
constexpr auto masterIntensityId = "masterIntensity";

juce::String describeTiming(const GrooveMorphAudioProcessor& processor)
{
    using groovemorph::core::TimingSource;
    namespace Missing = groovemorph::core::MissingField;

    if (!processor.isPrepared())
        return "Timing: audio not running";
    if (!processor.hasValidTiming())
        return "Timing: invalid manual settings";

    juce::String text = "Timing: ";
    switch (processor.getTimingSource())
    {
        case TimingSource::Host: text << "host"; break;
        case TimingSource::Mixed: text << "host + manual"; break;
        case TimingSource::Manual: text << "manual"; break;
        case TimingSource::FreeRun: text << "free-run"; break;
    }

    juce::StringArray missing;
    const auto fields = processor.getMissingTimingFields();
    if ((fields & Missing::tempo) != 0) missing.add("tempo");
    if ((fields & Missing::meter) != 0) missing.add("meter");
    if ((fields & Missing::playState) != 0) missing.add("play state");
    if ((fields & Missing::position) != 0) missing.add("position");
    if (!missing.isEmpty())
        text << " (host missing: " << missing.joinIntoString(", ") << ")";
    return text;
}
}

GrooveMorphAudioProcessorEditor::GrooveMorphAudioProcessorEditor(
    GrooveMorphAudioProcessor& processorToEdit)
    : AudioProcessorEditor(&processorToEdit), audioProcessor(processorToEdit)
{
    setResizable(true, true);
    setResizeLimits(320, 210, 900, 500);
    setSize(420, 210);

    titleLabel.setText("GrooveMorph", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));

    intensitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    intensitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 24);
    intensitySlider.setRange(0.0, 1.0, 0.01);
    intensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, masterIntensityId, intensitySlider);
    intensitySlider.onValueChange = [this] { updateValueLabel(); };

    valueLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(intensitySlider);
    timingLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(valueLabel);
    addAndMakeVisible(timingLabel);
    updateValueLabel();
    timerCallback();
    startTimerHz(10);
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
    timingLabel.setBounds(area.removeFromTop(26));
}

void GrooveMorphAudioProcessorEditor::timerCallback()
{
    timingLabel.setText(describeTiming(audioProcessor), juce::dontSendNotification);
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
