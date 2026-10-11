#pragma once

#include <JuceHeader.h>

#include "core/TransportResolver.h"

#include <atomic>
#include <cstdint>

class GrooveMorphAudioProcessor final : public juce::AudioProcessor
{
public:
    GrooveMorphAudioProcessor();
    ~GrooveMorphAudioProcessor() override = default;

    const juce::String getName() const override { return "GrooveMorph"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    // Latest resolved timing status, written by the audio thread for the editor to display.
    groovemorph::core::TimingSource getTimingSource() const noexcept
    {
        return static_cast<groovemorph::core::TimingSource>(timingSource.load(std::memory_order_relaxed));
    }
    std::uint32_t getMissingTimingFields() const noexcept
    {
        return missingTimingFields.load(std::memory_order_relaxed);
    }
    bool hasValidTiming() const noexcept { return timingValid.load(std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    groovemorph::core::HostTransport readHostTransport() const;

    double currentSampleRate = 0.0;
    std::int64_t freeRunSample = 0;
    std::atomic<int> timingSource{static_cast<int>(groovemorph::core::TimingSource::Manual)};
    std::atomic<std::uint32_t> missingTimingFields{0};
    std::atomic<bool> timingValid{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMorphAudioProcessor)
};
