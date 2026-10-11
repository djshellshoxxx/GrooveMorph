#include "PluginProcessor.h"

namespace
{
constexpr auto stateType = "GrooveMorphState";
constexpr auto masterIntensityId = "masterIntensity";
constexpr auto manualTempoId = "manualTempo";
constexpr auto manualMeterNumeratorId = "manualMeterNumerator";
constexpr auto manualMeterDenominatorId = "manualMeterDenominator";
constexpr auto phraseBarsId = "phraseBars";
constexpr int meterDenominators[] = {1, 2, 4, 8, 16};

float loadParameter(const juce::AudioProcessorValueTreeState& state, const char* id, float fallback)
{
    const auto* value = state.getRawParameterValue(id);
    return value != nullptr ? value->load(std::memory_order_relaxed) : fallback;
}
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
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{manualTempoId, 1},
        "Manual Tempo",
        juce::NormalisableRange<float>{20.0f, 300.0f, 0.01f},
        120.0f));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{manualMeterNumeratorId, 1}, "Manual Meter Numerator", 1, 16, 4));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{manualMeterDenominatorId, 1},
        "Manual Meter Denominator",
        juce::StringArray{"1", "2", "4", "8", "16"},
        2));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{phraseBarsId, 1}, "Phrase Bars", 1, 16, 4));
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
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;
    freeRunSample = 0;
    previousGain = loadParameter(parameters, masterIntensityId, 1.0f);
    setLatencySamples(0);
    prepared.store(true, std::memory_order_relaxed);
}

void GrooveMorphAudioProcessor::releaseResources()
{
    prepared.store(false, std::memory_order_relaxed);
}

groovemorph::core::HostTransport GrooveMorphAudioProcessor::readHostTransport() const
{
    groovemorph::core::HostTransport transport;
    auto* playHead = getPlayHead();
    if (playHead == nullptr)
        return transport;

    const auto position = playHead->getPosition();
    if (!position.hasValue())
        return transport;

    if (const auto bpm = position->getBpm())
        transport.tempoBpm = *bpm;
    if (const auto signature = position->getTimeSignature())
    {
        transport.meterNumerator = signature->numerator;
        transport.meterDenominator = signature->denominator;
    }
    transport.isPlaying = position->getIsPlaying();
    if (const auto samples = position->getTimeInSamples())
        transport.timeInSamples = *samples;
    return transport;
}

void GrooveMorphAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    const auto denominatorIndex = juce::jlimit(
        0, 4, static_cast<int>(loadParameter(parameters, manualMeterDenominatorId, 2.0f)));
    const groovemorph::core::PhraseGrid manualGrid{
        static_cast<double>(loadParameter(parameters, manualTempoId, 120.0f)),
        static_cast<int>(loadParameter(parameters, manualMeterNumeratorId, 4.0f)),
        meterDenominators[denominatorIndex],
        static_cast<int>(loadParameter(parameters, phraseBarsId, 4.0f)),
        0};
    const auto timing = groovemorph::core::TransportResolver::resolve(
        readHostTransport(), manualGrid, freeRunSample, currentSampleRate);
    timingValid.store(timing.has_value(), std::memory_order_relaxed);
    if (timing)
    {
        timingSource.store(static_cast<int>(timing->source), std::memory_order_relaxed);
        missingTimingFields.store(timing->missingFields, std::memory_order_relaxed);
    }
    freeRunSample += buffer.getNumSamples();

    const auto gain = loadParameter(parameters, masterIntensityId, 1.0f);
    const auto inputChannels = getTotalNumInputChannels();
    const auto outputChannels = getTotalNumOutputChannels();
    const auto channelsToProcess = juce::jmin(inputChannels, outputChannels, buffer.getNumChannels());

    for (int channel = channelsToProcess; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    for (int channel = 0; channel < channelsToProcess; ++channel)
        buffer.applyGainRamp(channel, 0, buffer.getNumSamples(), previousGain, gain);
    previousGain = gain;
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
