#include "PluginProcessor.h"

GrooveMorphAudioProcessor::GrooveMorphAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    masterIntensity = new juce::AudioParameterFloat(
        "masterIntensity",
        "Master Intensity",
        0.0f,
        1.0f,
        0.5f);

    addParameter(masterIntensity);
}

void GrooveMorphAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void GrooveMorphAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);

    const auto gain = masterIntensity->get();
    const auto numChannels = getNumOutputChannels();

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        const auto numSamples = buffer.getNumSamples();

        for (int sample = 0; sample < numSamples; ++sample)
            channelData[sample] *= gain;
    }

    if (getNumInputChannels() == 0)
    {
        // Standalone path: preserve output as dry signal with controlled gain.
    }
}

juce::AudioProcessorEditor* GrooveMorphAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

void GrooveMorphAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("GrooveMorph");
    state.setProperty("masterIntensity", masterIntensity->getValue(), nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void GrooveMorphAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml = getXmlFromBinary(data, sizeInBytes);

    if (xml != nullptr && xml->hasTagName("GrooveMorph"))
    {
        const auto value = xml->getDoubleAttribute("masterIntensity", 0.5);
        masterIntensity->setValueNotifyingHost(value);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrooveMorphAudioProcessor();
}
