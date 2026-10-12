#pragma once

#include <JuceHeader.h>

#include "core/TransportResolver.h"

#include <atomic>
#include <cstdint>

struct TimingStatusSnapshot
{
    groovemorph::core::TimingSource source = groovemorph::core::TimingSource::Manual;
    std::uint32_t missingFields = 0;
    bool valid = false;
};

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
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    // One packed atomic keeps the UI's timing source, missing fields, and validity
    // from the same completed audio callback.
    TimingStatusSnapshot getTimingStatus() const noexcept
    {
        const auto packed = timingStatus.load(std::memory_order_relaxed);
        return {
            static_cast<groovemorph::core::TimingSource>((packed >> sourceShift) & sourceMask),
            static_cast<std::uint32_t>(packed & missingMask),
            ((packed >> validShift) & 1u) != 0
        };
    }
    groovemorph::core::TimingSource getTimingSource() const noexcept
    {
        return getTimingStatus().source;
    }
    std::uint32_t getMissingTimingFields() const noexcept
    {
        return getTimingStatus().missingFields;
    }
    bool hasValidTiming() const noexcept { return getTimingStatus().valid; }
    bool isPrepared() const noexcept { return prepared.load(std::memory_order_relaxed); }
    bool hadStateRecovery() const noexcept { return stateRecoveryNeeded.load(std::memory_order_relaxed); }

private:
    static constexpr unsigned sourceShift = 32;
    static constexpr unsigned validShift = 34;
    static constexpr std::uint64_t sourceMask = 0x3u;
    static constexpr std::uint64_t missingMask = 0xffffffffu;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    groovemorph::core::HostTransport readHostTransport() const;
    void resetParametersToDefaults() noexcept;

    double currentSampleRate = 0.0;
    std::int64_t freeRunSample = 0;
    float currentGain = 0.0f;
    float targetGain = 0.0f;
    float gainStep = 0.0f;
    int gainRampRemaining = 0;
    std::atomic<float>* intensityParameter = nullptr;
    std::atomic<float>* tempoParameter = nullptr;
    std::atomic<float>* meterNumeratorParameter = nullptr;
    std::atomic<float>* meterDenominatorParameter = nullptr;
    std::atomic<float>* phraseBarsParameter = nullptr;
    std::atomic<bool> prepared{false};
    std::atomic<std::uint64_t> timingStatus{0};
    std::atomic<bool> stateRecoveryNeeded{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMorphAudioProcessor)
};
