#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto stateType = "GrooveMorphState";
constexpr auto masterIntensityId = "masterIntensity";
constexpr auto manualTempoId = "manualTempo";
constexpr auto manualMeterNumeratorId = "manualMeterNumerator";
constexpr auto manualMeterDenominatorId = "manualMeterDenominator";
constexpr auto phraseBarsId = "phraseBars";
constexpr int meterDenominators[] = {1, 2, 4, 8, 16};
constexpr int maximumStateBytes = 1024 * 1024;
constexpr double gainRampSeconds = 0.001;

float loadParameter(const std::atomic<float>* value, float fallback) noexcept
{
    return value != nullptr ? value->load(std::memory_order_relaxed) : fallback;
}
}

GrooveMorphAudioProcessor::GrooveMorphAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, stateType, createParameterLayout())
{
    intensityParameter = parameters.getRawParameterValue(masterIntensityId);
    tempoParameter = parameters.getRawParameterValue(manualTempoId);
    meterNumeratorParameter = parameters.getRawParameterValue(manualMeterNumeratorId);
    meterDenominatorParameter = parameters.getRawParameterValue(manualMeterDenominatorId);
    phraseBarsParameter = parameters.getRawParameterValue(phraseBarsId);
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
        juce::ParameterID{manualMeterNumeratorId, 1}, "Manual Meter Numerator", 1, 32, 4));
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
    currentGain = loadParameter(intensityParameter, 1.0f);
    targetGain = currentGain;
    gainStep = 0.0f;
    gainRampRemaining = 0;
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
    if (const auto ppq = position->getPpqPosition())
        transport.ppqPosition = *ppq;
    if (const auto barStart = position->getPpqPositionOfLastBarStart())
        transport.ppqPositionOfLastBarStart = *barStart;
    return transport;
}

void GrooveMorphAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    const auto denominatorIndex = juce::jlimit(
        0, 4, static_cast<int>(loadParameter(meterDenominatorParameter, 2.0f)));
    const groovemorph::core::PhraseGrid manualGrid{
        static_cast<double>(loadParameter(tempoParameter, 120.0f)),
        static_cast<int>(loadParameter(meterNumeratorParameter, 4.0f)),
        meterDenominators[denominatorIndex],
        static_cast<int>(loadParameter(phraseBarsParameter, 4.0f)),
        0};
    const auto timing = groovemorph::core::TransportResolver::resolve(
        readHostTransport(), manualGrid, freeRunSample, currentSampleRate);
    const auto source = timing ? timing->source : groovemorph::core::TimingSource::Manual;
    const auto missing = timing ? timing->missingFields : 0u;
    const auto packedTiming = static_cast<std::uint64_t>(missing)
        | (static_cast<std::uint64_t>(source) << 32)
        | (static_cast<std::uint64_t>(timing.has_value()) << 34);
    timingStatus.store(packedTiming, std::memory_order_relaxed);
    freeRunSample += buffer.getNumSamples();

    const auto gain = loadParameter(intensityParameter, 1.0f);
    if (gain != targetGain)
    {
        targetGain = gain;
        gainRampRemaining = juce::jmax(1, static_cast<int>(std::lround(currentSampleRate * gainRampSeconds)));
        gainStep = (targetGain - currentGain) / static_cast<float>(gainRampRemaining);
    }

    const auto inputChannels = getTotalNumInputChannels();
    const auto outputChannels = getTotalNumOutputChannels();
    const auto channelsToProcess = juce::jmin(inputChannels, outputChannels, buffer.getNumChannels());

    for (int channel = channelsToProcess; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        if (gainRampRemaining > 0)
        {
            currentGain += gainStep;
            if (--gainRampRemaining == 0)
                currentGain = targetGain;
        }
        for (int channel = 0; channel < channelsToProcess; ++channel)
            buffer.getWritePointer(channel)[sample] *= currentGain;
    }
}

void GrooveMorphAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void GrooveMorphAudioProcessor::resetParametersToDefaults() noexcept
{
    for (auto* parameter : getParameters())
        parameter->setValueNotifyingHost(parameter->getDefaultValue());
}

void GrooveMorphAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto recover = [this] {
        resetParametersToDefaults();
        stateRecoveryNeeded.store(true, std::memory_order_relaxed);
    };

    if (data == nullptr || sizeInBytes <= 0 || sizeInBytes > maximumStateBytes)
    {
        recover();
        return;
    }

    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(stateType))
    {
        recover();
        return;
    }

    const auto restoredState = juce::ValueTree::fromXml(*xml);
    if (!restoredState.isValid() || !restoredState.hasType(parameters.state.getType()))
    {
        recover();
        return;
    }

    parameters.replaceState(restoredState);
    stateRecoveryNeeded.store(false, std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrooveMorphAudioProcessor();
}
