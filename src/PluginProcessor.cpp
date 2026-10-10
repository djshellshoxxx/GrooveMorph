#include "PluginProcessor.h"

namespace
{
constexpr auto stateType = "GrooveMorphState";
constexpr auto masterIntensityId = "masterIntensity";
}

GrooveMorphAudioProcessor::GrooveMorphAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, stateType, createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
GrooveMorphAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{masterIntensityId, 1},
        "Master Intensity",
        juce::NormalisableRange<float>{0.0f, 1.0f},
        0.5f));
    return layout;
}

bool GrooveMorphAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void GrooveMorphAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    setLatencySamples(0);
}

void GrooveMorphAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    const auto* intensity = parameters.getRawParameterValue(masterIntensityId);
    const auto gain = intensity != nullptr ? intensity->load(std::memory_order_relaxed) : 1.0f;
    const auto inputChannels = getTotalNumInputChannels();
    const auto outputChannels = getTotalNumOutputChannels();
    const auto channelsToProcess = juce::jmin(inputChannels, outputChannels, buffer.getNumChannels());

    for (int channel = channelsToProcess; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    for (int channel = 0; channel < channelsToProcess; ++channel)
        buffer.applyGain(channel, 0, buffer.getNumSamples(), gain);
}

void GrooveMorphAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void GrooveMorphAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(stateType))
        return;

    const auto restoredState = juce::ValueTree::fromXml(*xml);
    if (restoredState.isValid() && restoredState.hasType(parameters.state.getType()))
        parameters.replaceState(restoredState);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrooveMorphAudioProcessor();
}
