#pragma once

#include <JuceHeader.h>

#include "core/Scheduler.h"
#include "core/control/AutoReturn.h"
#include "core/control/PlacementEvaluator.h"
#include "core/control/VariationBlend.h"
#include "core/TransportResolver.h"
#include "core/control/GestureLane.h"
#include "core/dsp/DemoSource.h"
#include "core/MidiLinks.h"
#include "core/dsp/LoopSource.h"
#include "core/variation/CandidateBuilder.h"
#include "core/dsp/OnsetDetector.h"
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
class GrooveMorphAudioProcessor final : public juce::AudioProcessor,
                                        private juce::Timer
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
    // Appended for the alpha (F02 morph, C05 automatic return). Never repurpose.
    static constexpr const char* morphTargetId = "morphTarget";
    static constexpr const char* morphAmountId = "morphAmount";
    static constexpr const char* autoReturnBarsId = "autoReturnBars";
    static constexpr const char* autoReturnTargetId = "autoReturnTarget";
    static constexpr const char* autoReturnUnitId = "autoReturnUnit";
    static constexpr const char* bandModeId = "bandMode";
    static constexpr const char* bandCrossoverId = "bandCrossover";
    static constexpr const char* audioTriggerId = "audioTrigger";
    static constexpr const char* audioTriggerSensId = "audioTriggerSens";

    static constexpr double bypassFadeSeconds = 0.010;

    GrooveMorphAudioProcessor();
    ~GrooveMorphAudioProcessor() override { stopTimer(); }

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

    // ---- Visual window telemetry (audio thread -> UI) ----
    // The ring records nothing until the Visual window activates it (setActive(true)).
    groovemorph::core::trace::TraceRing& getTraceRing() noexcept { return traceRing; }

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
    // Host Variation parameter semantics (A02): the parameter is the REQUESTED variation. While a
    // quantized request is pending it shows the request; once the request completes, is cancelled,
    // or the sounding variation changes through the GUI/MIDI/auto-return, the message thread
    // realigns it to the sounding variation. Never called from the audio thread (host
    // notification). Public so tests can drive it without a message loop.
    void syncHostVariationParameter();
    // Input source (F09): 0 = live input, 1 = built-in demo drum loop, 2 = captured/imported loop.
    // The demo and loop sources replace the input while the transport is playing. Not a host
    // parameter and not saved in project state.
    void setSourceMode(int mode) noexcept { sourceMode.store(juce::jlimit(0, 2, mode), std::memory_order_relaxed); }
    int getSourceMode() const noexcept { return sourceMode.load(std::memory_order_relaxed); }

    // ---- MIDI learn: link any control to a MIDI CC ----
    // Targets: "p:<parameterId>" or "s:<effect 0-4>:<depth|slice|enabled>" (the sounding scene). Message thread.
    void beginMidiLearn(const juce::String& target);   // the next CC that moves is linked to the target
    void cancelMidiLearn();
    juce::String getMidiLearnTarget() const;            // empty when not learning
    int getMidiLinkCc(const juce::String& target) const; // -1 when not linked
    bool linkMidi(int cc, const juce::String& target);
    void unlinkMidi(const juce::String& target);
    void clearMidiLinks();
    // Applies controller movement received since the last call (the processor's timer calls this ~20x/s).
    void servicePendingControllers() { applyMidiLinks(); }
    // Live scene edit for controllers: no undo entry per move, refused while a preview is active.
    void setOperationSettingsLive(std::size_t index, groovemorph::core::OperationType op,
                                  const groovemorph::core::OperationSettings& settings);

    // Restores every setting to its factory default (confirmation is the caller's job). Message thread.
    void resetToDefaults();

    // ---- Internal transport (Standalone, or Sample Clock when the host reports no play state) ----
    // Play continues from the current position; Stop halts and rewinds to the start. When a host decides
    // play/stop, isInternalTransportActive() is false and these requests have no effect on the audio.
    void startInternalTransport() noexcept { internalPlaying.store(true, std::memory_order_relaxed); }
    void stopInternalTransport() noexcept
    {
        internalPlaying.store(false, std::memory_order_relaxed);
        internalRewind.store(true, std::memory_order_relaxed);
    }
    bool isInternalPlaying() const noexcept { return internalPlaying.load(std::memory_order_relaxed); }
    bool isInternalTransportActive() const noexcept { return internalClockActive.load(std::memory_order_relaxed); }
    void setDemoSourceEnabled(bool enabled) noexcept { setSourceMode(enabled ? 1 : 0); }
    bool isDemoSourceEnabled() const noexcept { return getSourceMode() == 1; }
    // Loop capture/import (message thread). Capture records the pre-effect signal for N bars starting
    // on the next bar line while the transport plays; pollLoopCapture() (timer) publishes it.
    bool armLoopCapture(int bars);
    void cancelLoopCapture() noexcept { loopCapture.cancel(); }
    groovemorph::core::LoopCapture::State getLoopCaptureState() const noexcept { return loopCapture.state(); }
    // Why the last capture produced no loop: 0 none, 1 longer than 20 s, 2 transport stopped.
    int getLoopCaptureFailure() const noexcept { return loopCapture.failure(); }
    // Loads an audio file (WAV/AIFF/FLAC/...; up to 20 s) as a loop spanning `bars` bars.
    // bars: 1..16, or 0 = automatic. nativeBpm: the loop's own tempo, or 0 = unknown. nativeBpm wins over bars
    // (see core/dsp/LoopImport.h). The file is resampled to the device rate. The result is readable through
    // getLoopBeats()/getLoopBpm() for the UI to report what was decided.
    juce::Result importLoopFile(const juce::File& file, int bars, double nativeBpm = 0.0);
    double getLoopBeats() const noexcept { return loopBeatsStatus.load(std::memory_order_relaxed); }
    double getLoopBpm() const noexcept { return loopBpmStatus.load(std::memory_order_relaxed); }
    // Sets the manual tempo parameter to the loop's native tempo. False when there is no loop or the tempo is out of range.
    bool matchTempoToLoop();
    void clearLoop() noexcept;
    bool hasLoop() const noexcept { return loopSource.hasLoop(); }
    double getLoopSeconds() const noexcept { return loopSeconds.load(std::memory_order_relaxed); }
    void pollLoopCapture();
    // Gesture lane (F11, message thread): records one phrase of movement of a macro parameter (0 Morph
    // amount, 1 Intensity, 2 Dry/Wet) and plays it back by phrase position at timer rate (~20 Hz).
    void setGestureTarget(int target);
    int getGestureTarget() const noexcept { return gestureTarget; }
    void setGestureRecording(bool record);
    void setGesturePlaying(bool play);
    void clearGesture();
    bool isGestureRecording() const noexcept { return gestureRecording; }
    bool isGesturePlaying() const noexcept { return gesturePlaying; }
    int getGestureFill() const noexcept { return gesture.filledBins(); }
    void updateGesture();
    // Phrase evolution (F05): every N phrases the chosen scene is replaced, for sounding only, by a
    // deterministic candidate generated from its stored settings. Stored scenes and saved state are
    // never changed, and the sequence is reproducible from the seed.
    struct EvolutionSettings { bool enabled; std::size_t scene; int everyPhrases; float distance; int style; };
    EvolutionSettings getEvolutionSettings() const;
    void setEvolution(bool enabled, std::size_t scene, int everyPhrases, float distance, int style,
                      std::uint64_t seed);
    bool isEvolving() const;
    std::uint32_t getEvolutionStep() const;
    void updateEvolution();
    // Grid used by the phrase editor: the audio thread's resolved meter/phrase when available,
    // otherwise the manual parameters.
    groovemorph::core::control::GridContext getGridContext() const noexcept;
    // MIDI macros (F12): when enabled, CC 20/21/22 drive Intensity / Dry-Wet / Morph amount and
    // program change 0-3 requests scenes. CC values are applied to the host parameters on the
    // message thread (applyMidiMacros), never from the audio thread. Not saved in project state.
    void setMidiMacrosEnabled(bool enabled) noexcept { midiMacros.store(enabled, std::memory_order_relaxed); }
    bool areMidiMacrosEnabled() const noexcept { return midiMacros.load(std::memory_order_relaxed); }
    void applyMidiMacros();
    // Number of audio-trigger onsets detected (F07), for the UI meter.
    std::uint32_t getOnsetCount() const noexcept { return onsetCount.load(std::memory_order_relaxed); }
    // Automatic-return status for the UI: scene index the pending return will switch to, or -1.
    int getAutoReturnTarget() const noexcept { return autoReturnTargetStatus.load(std::memory_order_relaxed); }
    // Queues a switch using the current transitionMode parameter.
    void requestVariation(std::size_t index) noexcept;
    void triggerManualSwitch() noexcept { uiTrigger.store(true, std::memory_order_release); }
    void cancelQueuedSwitch() noexcept { uiCancel.store(true, std::memory_order_release); }

    // ---- Scene bank editing (message thread) ----
    groovemorph::core::scene::SceneBank getSceneBank() const;
    void setVariationSettings(std::size_t index, const groovemorph::core::VariationSettings& settings);
    // One undoable settings-only copy, performed under the model lock off the audio thread.
    // Disallowed while auditioning to keep preview/apply ownership unambiguous.
    [[nodiscard]] bool copyVariationSettings(std::size_t source, std::size_t destination);
    void setOperationSettings(std::size_t index, groovemorph::core::OperationType op,
                              const groovemorph::core::OperationSettings& settings);
    void setVariationName(std::size_t index, std::string_view name);
    void setMidiNoteMap(const groovemorph::core::scene::MidiNoteMap& map);
    void setSceneSeed(std::uint64_t seed);
    // Entire-scene duplicate (settings, placement/lane/permissions and name). One undo entry.
    [[nodiscard]] bool duplicateVariation(std::size_t source, std::size_t destination);
    // Factory-reset one scene. One undo entry.
    void resetVariation(std::size_t index);
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

    void timerCallback() override
    {
        syncHostVariationParameter();
        applyMidiMacros();
        applyMidiLinks();
        pollLoopCapture();
        updateGesture();
        updateEvolution();
    }
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
    // Effective settings for a scene: morph blend (F02) applied; placement is applied per slice.
    groovemorph::core::VariationSettings effectiveSettings(std::size_t variation) const noexcept;
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
        std::atomic<float>* morphTarget = nullptr;
        std::atomic<float>* morphAmount = nullptr;
        std::atomic<float>* autoReturnBars = nullptr;
        std::atomic<float>* autoReturnTarget = nullptr;
        std::atomic<float>* autoReturnUnit = nullptr;
        std::atomic<float>* bandMode = nullptr;
        std::atomic<float>* bandCrossover = nullptr;
        std::atomic<float>* audioTrigger = nullptr;
        std::atomic<float>* audioTriggerSens = nullptr;
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
    // Telemetry for the Visual window; written by the engine only while active (see TraceRing).
    groovemorph::core::trace::TraceRing traceRing;
    groovemorph::core::RhythmEngine engine;
    groovemorph::core::scene::MidiTriggerState midiTrigger;
    juce::AudioBuffer<float> dryBuffer;
    int maxBlock = 0;
    double currentSampleRate = 0.0;
    // Internal (Standalone / Sample Clock) transport. freeRunSample counts samples played; the grid origin is
    // re-based when tempo or meter change so the musical position stays continuous (never jumps).
    std::int64_t freeRunSample = 0;
    std::int64_t freeRunOrigin = 0;
    long double freeRunSamplesPerBeat = 0.0L;
    std::atomic<bool> internalPlaying{true};
    std::atomic<bool> internalRewind{false};
    std::atomic<bool> internalClockActive{false}; // audio thread: the internal transport is what decides play/stop
    std::optional<std::int64_t> previousHostSamplePosition;
    std::optional<double> previousHostPpqPosition;
    double previousHostTempo = 120.0;
    int previousHostBlockSamples = 0;
    bool previousHostWasPlaying = false;
    float bypassGain = 0.0f; // 0 = processed, 1 = dry
    float bypassStep = 1.0f;
    int lastSeenVariationParam = 0;
    groovemorph::core::control::AutoReturn autoReturn;
    int morphTargetIndex = -1;   // -1 = off; scene whose settings are blended in
    float morphWeight = 0.0f;
    // F03/F10 band split (first-order complementary crossover) and F07 audio trigger.
    int bandModeSel = 0;          // 0 full band, 1 protect lows, 2 lows only
    float bandCoefficient = 0.0f;
    std::array<float, 2> bandLowState{};
    juce::AudioBuffer<float> bandBypass;
    std::array<groovemorph::core::OnsetDetector, 2> onsetDetectors;
    int audioTriggerSel = 0;      // 0 off, 1 next scene, 2 trigger queued
    float audioTriggerSensitivity = 0.5f;
    bool onsetThisBlock = false;

    // UI -> audio.
    std::atomic<int> uiRequest{-1};
    std::atomic<int> uiReset{-1};
    std::atomic<bool> uiResyncParam{false};
    std::atomic<int> expectedParamEcho{-1}; // value the message thread wrote to the host param
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
    std::atomic<int> paramSeenByAudio{-1};  // variation parameter value the audio thread last read
    std::atomic<int> autoReturnTargetStatus{-1};
    std::atomic<int> sourceMode{0};
    groovemorph::core::LoopSource loopSource;
    groovemorph::core::LoopCapture loopCapture;
    std::atomic<double> loopSeconds{0.0};
    // Message-thread copy of the loop at its sample rate, so it survives prepareToPlay (guarded by loopServiceMutex).
    juce::AudioBuffer<float> loopStore;
    double loopStoreRate = 0.0;
    double loopStoreBeats = 0.0;
    bool republishStoredLoop();
    std::atomic<double> loopBeatsStatus{0.0};
    std::atomic<double> loopBpmStatus{0.0};
    std::atomic<double> sampleRateStatus{48000.0};
    mutable std::mutex loopServiceMutex; // message-thread loop services vs prepareToPlay; never taken by the audio thread
    std::atomic<double> phraseFractionStatus{0.0};
    std::atomic<std::int64_t> phraseIndexStatus{0};
    groovemorph::core::control::GestureLane gesture;
    int gestureTarget = 0;
    bool gestureRecording = false, gesturePlaying = false;
    // Evolution state (guarded by modelMutex).
    bool evolutionOn = false;
    std::size_t evolutionScene = 0;
    int evolutionEvery = 1;
    groovemorph::core::variation::GeneratorSettings evolutionSettings;
    std::uint32_t evolutionStep = 0;
    std::int64_t lastEvolutionPhrase = -1;
    // MIDI links: the audio thread only pushes CC values into the inbox; links are applied by the timer.
    groovemorph::core::CcInbox ccInbox;
    mutable std::mutex midiLinkMutex;
    groovemorph::core::MidiLinkTable midiLinks;
    juce::String midiLearnTarget;
    void applyMidiLinks();
    void applyLinkedValue(const std::string& target, float normalised);
    std::atomic<bool> midiMacros{false};
    std::array<std::atomic<float>, 3> ccValue{};
    std::atomic<unsigned> ccDirty{0};
    std::atomic<std::uint32_t> onsetCount{0};
    std::atomic<int> resolvedBeatsPerBar{0};
    std::atomic<int> resolvedPhraseBars{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMorphAudioProcessor)
};
