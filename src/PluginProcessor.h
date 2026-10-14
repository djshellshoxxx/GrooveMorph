#pragma once

#include <JuceHeader.h>

#include "core/Scheduler.h"
#include "core/TransportResolver.h"
#include "core/dsp/RhythmEngine.h"
#include "core/scene/AuditionSession.h"
#include "core/scene/MidiNoteMap.h"
#include "core/scene/SceneBank.h"
#include "core/scene/SceneSerializer.h"
#include "core/scene/UndoStack.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

// Host adapter: parameters, state, timing, scheduling, MIDI triggers and the rhythm engine.
//
// Threading:
//  - The scene model (undo stack, audition) lives on the message thread behind modelMutex.
//    Every edit publishes an immutable SceneSnapshot; the audio thread picks it up at the
//    next block start with a SpinLock try-lock (never waits; keeps the previous copy on
//    contention).
//  - UI switch requests/trigger/cancel are marshalled to the audio-thread-owned Scheduler
//    through atomics. Audio-thread status is published back through relaxed atomics.
//
// Operation settings (enable/depth/slice per variation and operation) are scene-bank state,
// not host parameters (spec 9: MIDI mappings and scene contents are state data).
class GrooveMorphAudioProcessor final : public juce::AudioProcessor
{
public:
    // Stable host parameter IDs (GM-FR-007). Never repurpose.
    static constexpr const char* masterIntensityId = "masterIntensity";
    static constexpr const char* dryWetId = "dryWet";
    static constexpr const char* manualTempoId = "manualTempo";
    static constexpr const char* manualMeterNumeratorId = "manualMeterNumerator";
    static constexpr const char* manualMeterDenominatorId = "manualMeterDenominator";
    static constexpr const char* phraseBarsId = "phraseBars";
    static constexpr const char* activeVariationId = "activeVariation";
    static constexpr const char* transitionModeId = "transitionMode";
    static constexpr const char* timingModeId = "timingMode";
    static constexpr const char* bypassId = "bypass";

    static constexpr double bypassFadeSeconds = 0.010;

    GrooveMorphAudioProcessor();
    ~GrooveMorphAudioProcessor() override = default;

    const juce::String getName() const override { return "GrooveMorph"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // The causal history stages can replay audio for up to four seconds after
    // the source falls silent. Report a conservative tail to DAW freeze/render
    // scheduling without adding latency or changing the audio callback.
    double getTailLengthSeconds() const override
    {
        return groovemorph::core::RhythmEngine::maxHistorySeconds
            + groovemorph::core::RhythmEngine::crossfadeSeconds;
    }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    // ---- Timing status (audio thread -> UI) ----
    groovemorph::core::TimingSource getTimingSource() const noexcept
    {
        return static_cast<groovemorph::core::TimingSource>(timingSource.load(std::memory_order_relaxed));
    }
    std::uint32_t getMissingTimingFields() const noexcept
    {
        return missingTimingFields.load(std::memory_order_relaxed);
    }
    bool hasValidTiming() const noexcept { return timingValid.load(std::memory_order_relaxed); }
    bool isPrepared() const noexcept { return prepared.load(std::memory_order_relaxed); }
    bool isTransportPlaying() const noexcept { return transportPlaying.load(std::memory_order_relaxed); }
    // Playhead display: fractional bar inside the phrase [0, phraseBars) and beat inside the
    // bar [0, meterNumerator), at the start of the last processed block.
    double getBarInPhrase() const noexcept { return barInPhrase.load(std::memory_order_relaxed); }
    double getBeatInBar() const noexcept { return beatInBar.load(std::memory_order_relaxed); }

    // ---- Variation switching (any thread -> audio thread) ----
    std::size_t getActiveVariation() const noexcept { return activeVariation.load(std::memory_order_relaxed); }
    // -1 when nothing is queued.
    int getQueuedVariation() const noexcept { return queuedVariation.load(std::memory_order_relaxed); }
    // Samples after the end of the last processed block until the queued switch; -1 when
    // unknown (nothing queued, untriggered Manual, or stopped transport).
    std::int64_t getSamplesUntilSwitch() const noexcept { return samplesUntilSwitch.load(std::memory_order_relaxed); }
    // Timeline sample (resolved position) of the most recent switch; -1 before any switch.
    std::int64_t getLastSwitchSample() const noexcept { return lastSwitchSample.load(std::memory_order_relaxed); }
    // Number of MIDI note events seen (lets the UI show "MIDI unavailable" when it stays 0).
    std::uint32_t getMidiNoteEventCount() const noexcept { return midiNoteEvents.load(std::memory_order_relaxed); }
    // Queues a switch using the current transitionMode parameter.
    void requestVariation(std::size_t index) noexcept;
    void triggerManualSwitch() noexcept { uiTrigger.store(true, std::memory_order_release); }
    void cancelQueuedSwitch() noexcept { uiCancel.store(true, std::memory_order_release); }

    // ---- Scene bank editing (message thread) ----
    groovemorph::core::scene::SceneBank getSceneBank() const;
    void setVariationSettings(std::size_t index, const groovemorph::core::VariationSettings& settings);
    void setOperationSettings(std::size_t index, groovemorph::core::OperationType op,
                              const groovemorph::core::OperationSettings& settings);
    void setVariationName(std::size_t index, std::string_view name);
    void setMidiNoteMap(const groovemorph::core::scene::MidiNoteMap& map);
    void setSceneSeed(std::uint64_t seed);
    bool undoSceneEdit();
    bool redoSceneEdit();
    bool canUndoSceneEdit() const;
    bool canRedoSceneEdit() const;

    // ---- Audition / compare (message thread) ----
    // While auditioning, the audio thread plays the audition variation (edited or stored).
    void beginAudition(std::size_t index);
    void setAuditionEdit(const groovemorph::core::scene::Variation& edited);
    void setAuditionComparingStored(bool hearStored);
    void toggleAuditionCompare();
    bool isAuditioning() const;
    bool isAuditionDirty() const;
    bool commitAudition(); // writes to the bank as one undoable edit
    void revertAudition();

    // Result of the last scene-state restore (GM-FR-010 recovery reporting).
    groovemorph::core::scene::LoadReport getLastSceneLoadReport() const;

private:
    struct PublishedScene
    {
        groovemorph::core::scene::SceneSnapshot snapshot{};
        bool auditioning = false;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    groovemorph::core::HostTransport readHostTransport() const;
    groovemorph::core::TransitionMode currentTransitionMode() const noexcept;

    // Message thread; caller holds modelMutex.
    void commitBankLocked(const groovemorph::core::scene::SceneBank& next);
    void publishSceneLocked();

    // Audio thread.
    void pullScene() noexcept;
    void handleMidi(const juce::uint8* data, int numBytes, groovemorph::core::TransitionMode mode) noexcept;
    void renderSegment(juce::AudioBuffer<float>& buffer, int numChannels, int start, int length,
                       const std::optional<groovemorph::core::ResolvedTiming>& blockTiming,
                       std::size_t variation, float wetAmount, float bypassTarget) noexcept;
    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool forceBypass) noexcept;

    // Cache APVTS lookup results once; the callback only loads atomic scalars.
    struct ParameterCache
    {
        std::atomic<float>* masterIntensity = nullptr;
        std::atomic<float>* dryWet = nullptr;
        std::atomic<float>* manualTempo = nullptr;
        std::atomic<float>* manualMeterNumerator = nullptr;
        std::atomic<float>* manualMeterDenominator = nullptr;
        std::atomic<float>* phraseBars = nullptr;
        std::atomic<float>* activeVariation = nullptr;
        std::atomic<float>* transitionMode = nullptr;
        std::atomic<float>* timingMode = nullptr;
    } cached;
    juce::AudioParameterBool* bypassParameter = nullptr;

    // Message-thread model.
    mutable std::mutex modelMutex;
    groovemorph::core::scene::UndoStack undoStack;
    groovemorph::core::scene::AuditionSession audition;
    groovemorph::core::scene::LoadReport lastLoadReport;

    // Snapshot handoff.
    juce::SpinLock sceneLock;
    PublishedScene sharedScene;
    std::atomic<std::uint32_t> sceneVersion{1};
    std::uint32_t audioSceneVersion = 0;
    PublishedScene audioScene;

    // Audio-thread state.
    groovemorph::core::Scheduler scheduler;
    groovemorph::core::RhythmEngine engine;
    groovemorph::core::scene::MidiTriggerState midiTrigger;
    juce::AudioBuffer<float> dryBuffer;
    int maxBlock = 0;
    double currentSampleRate = 0.0;
    std::int64_t freeRunSample = 0;
    std::optional<std::int64_t> previousHostSamplePosition;
    std::optional<double> previousHostPpqPosition;
    double previousHostTempo = 120.0;
    int previousHostBlockSamples = 0;
    bool previousHostWasPlaying = false;
    float bypassGain = 0.0f; // 0 = processed, 1 = dry
    float bypassStep = 1.0f;
    int lastSeenVariationParam = 0;

    // UI -> audio.
    std::atomic<int> uiRequest{-1};
    std::atomic<int> uiReset{-1};
    std::atomic<bool> uiResyncParam{false};
    std::atomic<bool> uiTrigger{false};
    std::atomic<bool> uiCancel{false};

    // Audio -> UI.
    std::atomic<bool> prepared{false};
    std::atomic<int> timingSource{static_cast<int>(groovemorph::core::TimingSource::Manual)};
    std::atomic<std::uint32_t> missingTimingFields{0};
    std::atomic<bool> timingValid{false};
    std::atomic<bool> transportPlaying{false};
    std::atomic<double> barInPhrase{0.0};
    std::atomic<double> beatInBar{0.0};
    std::atomic<std::size_t> activeVariation{0};
    std::atomic<int> queuedVariation{-1};
    std::atomic<std::int64_t> samplesUntilSwitch{-1};
    std::atomic<std::int64_t> lastSwitchSample{-1};
    std::atomic<std::uint32_t> midiNoteEvents{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMorphAudioProcessor)
};
