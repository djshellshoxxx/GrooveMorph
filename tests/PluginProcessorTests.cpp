#include "PluginProcessor.h"

#include <array>
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

using groovemorph::core::TimingSource;
namespace Missing = groovemorph::core::MissingField;

namespace
{
int failures = 0;

void expect(std::string_view name, bool condition)
{
    if (!condition)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

class FakePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

void setParameter(GrooveMorphAudioProcessor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.parameters.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

float getParameter(const GrooveMorphAudioProcessor& processor, const char* id)
{
    return processor.parameters.getRawParameterValue(id)->load();
}

void processSilence(GrooveMorphAudioProcessor& processor, int samples)
{
    juce::AudioBuffer<float> buffer(2, samples);
    buffer.clear();
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
}

std::vector<float> renderGainRamp(const std::vector<int>& blocks)
{
    GrooveMorphAudioProcessor processor;
    setParameter(processor, "masterIntensity", 1.0f);
    processor.prepareToPlay(48000.0, 256);
    setParameter(processor, "masterIntensity", 0.0f);

    std::vector<float> output;
    output.reserve(480);
    for (const auto count : blocks)
    {
        juce::AudioBuffer<float> buffer(2, count);
        for (int channel = 0; channel < 2; ++channel)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), 1.0f, count);
        juce::MidiBuffer midi;
        processor.processBlock(buffer, midi);
        for (int sample = 0; sample < count; ++sample)
            output.push_back(buffer.getSample(0, sample));
    }
    return output;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    {
        GrooveMorphAudioProcessor processor;
        processor.prepareToPlay(48000.0, 256);
        expect("zero latency", processor.getLatencySamples() == 0);

        auto layout = processor.getBusesLayout();
        expect("stereo layout supported", processor.checkBusesLayoutSupported(layout));
        layout.inputBuses.getReference(0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference(0) = juce::AudioChannelSet::mono();
        expect("mono layout supported", processor.checkBusesLayoutSupported(layout));
        layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
        expect("mono-to-stereo rejected", !processor.checkBusesLayoutSupported(layout));
    }

    {
        GrooveMorphAudioProcessor source;
        setParameter(source, "masterIntensity", 0.25f);
        setParameter(source, "manualTempo", 97.5f);
        setParameter(source, "manualMeterNumerator", 7.0f);
        setParameter(source, "phraseBars", 8.0f);
        juce::MemoryBlock state;
        source.getStateInformation(state);

        GrooveMorphAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        expect("state restores intensity", std::abs(getParameter(restored, "masterIntensity") - 0.25f) < 1.0e-4f);
        expect("state restores tempo", std::abs(getParameter(restored, "manualTempo") - 97.5f) < 1.0e-2f);
        expect("state restores meter", getParameter(restored, "manualMeterNumerator") == 7.0f);
        expect("state restores phrase bars", getParameter(restored, "phraseBars") == 8.0f);
        expect("valid state clears recovery status", !restored.hadStateRecovery());

        const char garbage[] = "not a state";
        restored.setStateInformation(garbage, sizeof(garbage));
        expect("malformed state reports recovery", restored.hadStateRecovery());
        expect("malformed state resets tempo default", std::abs(getParameter(restored, "manualTempo") - 120.0f) < 1.0e-2f);
        expect("malformed state resets intensity default", std::abs(getParameter(restored, "masterIntensity") - 0.5f) < 1.0e-4f);

        std::vector<char> oversized(1024 * 1024 + 1, 'x');
        restored.setStateInformation(oversized.data(), static_cast<int>(oversized.size()));
        expect("oversized state rejected and recovered", restored.hadStateRecovery());
    }

    {
        GrooveMorphAudioProcessor processor;
        expect("not prepared before prepareToPlay", !processor.isPrepared());
        processor.prepareToPlay(48000.0, 256);
        processSilence(processor, 256);
        expect("no playhead uses manual timing", processor.hasValidTiming()
                    && processor.getTimingSource() == TimingSource::Manual);
        expect("missing host play state is reported",
               (processor.getMissingTimingFields() & Missing::playState) != 0);

        FakePlayHead playHead;
        playHead.info.setBpm(128.0);
        playHead.info.setTimeSignature(juce::AudioPlayHead::TimeSignature{3, 4});
        playHead.info.setIsPlaying(true);
        playHead.info.setTimeInSamples(-1024);
        playHead.info.setPpqPosition(-0.25);
        playHead.info.setPpqPositionOfLastBarStart(-4.0);
        processor.setPlayHead(&playHead);
        processSilence(processor, 256);
        expect("host preroll and musical position are accepted",
               processor.getTimingSource() == TimingSource::Host
                   && processor.getMissingTimingFields() == 0);

        playHead.info.setTimeSignature(juce::nullopt);
        processSilence(processor, 256);
        expect("missing meter reported", processor.getTimingSource() == TimingSource::Mixed
                                             && processor.getMissingTimingFields() == Missing::meter);
        processor.setPlayHead(nullptr);
        processor.releaseResources();
        expect("not prepared after release", !processor.isPrepared());
    }

    {
        const auto singleBlock = renderGainRamp({480});
        const auto splitBlocks = renderGainRamp({64, 256, 160});
        expect("gain smoothing output size", singleBlock.size() == 480 && splitBlocks.size() == 480);
        bool blockInvariant = singleBlock.size() == splitBlocks.size();
        for (std::size_t i = 0; blockInvariant && i < singleBlock.size(); ++i)
            blockInvariant = std::abs(singleBlock[i] - splitBlocks[i]) < 1.0e-6f;
        expect("gain ramp is sample-domain and block-size invariant", blockInvariant);
        expect("ramp starts near previous gain", singleBlock.front() > 0.9f);
        expect("1 ms ramp reaches target", singleBlock.back() < 0.05f);
    }

    if (failures == 0)
        std::cout << "PluginProcessorTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
