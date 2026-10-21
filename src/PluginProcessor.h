#pragma once

#include <JuceHeader.h>

#include "core/Scheduler.h"
#include "core/control/AutoReturn.h"
#include "core/control/OperationTrims.h"
#include "core/control/PhaseResolver.h"
#include "core/control/LaunchPhase.h"
#include "core/control/PlacementEvaluator.h"
#include "core/dsp/Crossover.h"
#include "core/dsp/InputSanitizer.h"
#include "ServiceTimer.h"
#include "core/control/VariationBlend.h"
#include "core/TransportResolver.h"
#include "core/control/GestureLane.h"
#include "core/dsp/DemoSource.h"
#include "core/MidiLinks.h"
#include "core/dsp/LoopBlob.h"
#include "core/dsp/LoopSource.h"
#include "core/dsp/TimeStretcher.h"
#include "core/dsp/WaveformPeaks.h"
#include "core/dsp/OutputCapture.h"
#include "core/variation/CandidateBuilder.h"
#include "core/dsp/OnsetDetector.h"
#include "core/dsp/RhythmEngine.h"
#include "core/scene/AuditionSession.h"
#include "core/scene/EditCoalescing.h"
#include "core/scene/MidiNoteMap.h"
#include "core/scene/SceneBank.h"
#include "core/scene/SceneSerializer.h"
#include "core/scene/UndoStack.h"
#include "PresetManager.h"

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
    // Appended for the alpha (F02 morph, C05 automatic return). Never repurpose.
    static constexpr const char* morphTargetId = "morphTarget";
    static constexpr const char* morphAmountId = "morphAmount";
    static constexpr const char* autoReturnBarsId = "autoReturnBars";
    static constexpr const char* autoReturnTargetId = "autoReturnTarget";
    static constexpr const char* autoReturnUnitId = "autoReturnUnit";
    static constexpr const char* bandModeId = "bandMode";
    static constexpr const char* bandCrossoverId = "bandCrossover";
    static constexpr const char* bandSlopeId = "bandSlope";
    static constexpr const char* audioTriggerId = "audioTrigger";
    static constexpr const char* audioTriggerSensId = "audioTriggerSens";
    // Appended for the beta (A06): live trims of the sounding scene's per-operation controls. The IDs are
    // generated from the operation index (core::control::operationTrimId: "op0Enable", "op0Depth",
    // "op0Slice", ... one set per operation) and are never renumbered or reused.
    // Appended for N05 (Chaos macro). Never repurpose.
    static constexpr const char* chaosId = "chaos";
    static constexpr const char* loopStretchId = "loopStretch";
    // Appended in the Beta (W3): crossover topology (Classic / Linkwitz-Riley). Never repurpose.
    static constexpr const char* bandTopologyId = "bandTopology";

    static constexpr double bypassFadeSeconds = 0.010;

    GrooveMorphAudioProcessor();
    // Detaches the service timer first (waits for a tick in flight; safe from any thread, see ServiceTimer.h).
    ~GrooveMorphAudioProcessor() override { serviceTimer.detach(); }
    // Starts the 20 Hz message-thread services (idempotent, any thread). Called lazily from prepareToPlay,
    // createEditor and the message-thread service entry points; the constructor starts nothing.
    void ensureServices() { serviceTimer.ensureStarted(); }
    bool areServicesRunning() const noexcept { return serviceTimer.isRunning(); }

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

    // N02: host programs = factory presets then user presets (see docs/specs/N02-PRESETS.md).
    int getNumPrograms() override { return juce::jmax(1, presetManager.count()); }
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override { (void) applyPreset(index); }
    const juce::String getProgramName(int index) override { return presetManager.nameAt(index); }
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
    // Why the last link attempt failed, for the UI: 0 none, 1 the 64-link table is full. Reading clears it.
    int consumeMidiLinkNotice() noexcept { return midiLinkNotice.exchange(0, std::memory_order_relaxed); }
    int getMidiLinkCount() const;
    static constexpr int midiLinkNoticeTableFull = 1;
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
    // trimToSeconds > 0 uses only that much of the start of the file (a print with a tail); 0 = whole file.
    juce::Result importLoopFile(const juce::File& file, int bars, double nativeBpm = 0.0, double trimToSeconds = 0.0);

    // ---- Print to WAV (N04, message thread) ----
    // Records the processed output for `bars` bars from the next bar line while the transport plays.
    // The panel polls takePrintResult() (about 20 Hz) and writes the file; see core/dsp/OutputCapture.h.
    struct PrintResult
    {
        juce::AudioBuffer<float> audio;  // stereo, body + optional tail
        double sampleRate = 48000.0;
        int bodySamples = 0;             // bar-aligned part (without the tail)
        int bars = 0;
        int beatsPerBar = 4;
        double bpm = 120.0;              // tempo at the start of the print
        bool wetOnly = false;
        bool wetOnlyApplied = false;     // false when wet-only was requested but Intensity * Dry/Wet was too small for a block
        juce::String sceneName;
    };
    bool armPrint(int bars, bool wetOnly, bool includeTail);
    void cancelPrint() noexcept { outputCapture.cancel(); }
    groovemorph::core::OutputCapture::State getPrintState() const noexcept { return outputCapture.state(); }
    groovemorph::core::OutputCapture::Failure getPrintFailure() const noexcept { return outputCapture.failure(); }
    double getPrintProgress() const noexcept { return outputCapture.progress(); }
    // True once and fills `result` when a print has finished; also acknowledges (frees) an aborted one.
    bool takePrintResult(PrintResult& result);
    // Tail length that "Include tail" appends, in seconds.
    double getPrintTailSeconds() const noexcept { return getTailLengthSeconds(); }
    double getLoopBeats() const noexcept { return loopBeatsStatus.load(std::memory_order_relaxed); }
    double getLoopBpm() const noexcept { return loopBpmStatus.load(std::memory_order_relaxed); }
    // Sets the manual tempo parameter to the loop's native tempo. False when there is no loop or the tempo is out of range.
    bool matchTempoToLoop();
    void clearLoop() noexcept;
    bool hasLoop() const noexcept { return loopSource.hasLoop(); }
    double getLoopSeconds() const noexcept { return loopSeconds.load(std::memory_order_relaxed); }
    void pollLoopCapture();
    // F09 follow-up (PluginProcessorLoop.cpp). "Stretch to host tempo": a background WSOLA render of the loop
    // replaces the varispeed buffer when the tempo settles (off by default = beat-locked varispeed).
    bool isLoopStretchEnabled() const noexcept { return loadStretchFlag(); }
    double getLoopStretchRatio() const noexcept { return loopStretchRatioStatus.load(std::memory_order_relaxed); }
    bool isLoopStretchLimited() const noexcept { return loopStretchLimitedStatus.load(std::memory_order_relaxed); }
    bool isLoopStretchBusy() const noexcept { return stretchWorker.busy(); }
    // 0 none, 1 the loop is too large to be saved with the project, 2 saving failed, 3 a saved loop could not be restored.
    int getLoopPersistWarning() const noexcept { return loopPersistWarning.load(std::memory_order_relaxed); }
    // Waveform overview of the stored loop: min/max columns (0 = no loop). Cached per loop version.
    int getLoopPeaks(int columns, std::vector<float>& minimums, std::vector<float>& maximums) const;
    // Playhead 0..1 inside the loop while the loop source is playing, otherwise -1.
    double getLoopPlayFraction() const noexcept;
    // Timeline beat at which the stored loop starts (modulo its length).
    double getLoopOriginBeats() const noexcept { return loopSource.publishedOrigin(); }
    // Timer work: finished stretch jobs, new jobs after a tempo change, retrying a failed loop publish.
    void updateLoopStretch();
    // Project state (getStateInformation / setStateInformation).
    void appendLoopState(juce::ValueTree& state);
    void restoreLoopState(juce::ValueTree& restoredState);
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
    // ---- N05 Easy mode (implemented in PluginProcessorEasy.cpp) ----
    // Which editor view is shown. Saved in plugin state (child "Ui"): ON for a fresh instance, OFF for state
    // saved before N05. Message thread; not a host parameter.
    bool isEasyMode() const noexcept { return easyMode.load(std::memory_order_relaxed); }
    void setEasyMode(bool on) noexcept { easyMode.store(on, std::memory_order_relaxed); }
    // "Surprise me": builds a constrained candidate for the sounding scene, previews it (audition) and keeps
    // it at the next phrase boundary as one undoable edit. Message thread.
    enum class SurpriseStatus { Armed, NothingToChange, Busy };
    [[nodiscard]] SurpriseStatus requestSurprise();
    void cancelSurprise();          // reverts the pending preview; no-op when nothing is pending
    bool isSurprisePending() const;
    void updateSurprise();          // commits at the phrase boundary (processor timer)
    // Panic: no overlays, no pending changes. Stored scenes, intensity and the transport are untouched.
    void panicEasy();
    std::int64_t getPhraseIndex() const noexcept { return phraseIndexStatus.load(std::memory_order_relaxed); }
    // Queues a switch using the current transitionMode parameter.
    void requestVariation(std::size_t index) noexcept;
    void triggerManualSwitch() noexcept { uiTrigger.store(true, std::memory_order_release); }
    void cancelQueuedSwitch() noexcept { uiCancel.store(true, std::memory_order_release); }
    // C04: restart the active scene's pattern now (transport and variation are unchanged).
    void restartActiveScene() noexcept { uiRestart.store(true, std::memory_order_release); }
    // Pattern-phase shift (meter beats) applied since the last switch/restart; 0 = following the transport.
    // C05 "Dry" return: 0 none, 1 armed (waiting for the boundary), 2 active (effects off until a scene request).
    // C04: smoothing time (ms) of the scene that launched last; 0 = engine default.
    float getTransitionSmoothingMs() const noexcept { return smoothingStatus.load(std::memory_order_relaxed); }
    int getDryReturnState() const noexcept { return dryReturnStatus.load(std::memory_order_relaxed); }
    // Count of non-finite input samples replaced by the input sanitizer (diagnostics/tests).
    std::uint32_t getNonFiniteSampleCount() const noexcept { return nonFiniteSamples.load(std::memory_order_relaxed); }
    double getPhaseShiftBeats() const noexcept { return phaseShiftStatus.load(std::memory_order_relaxed); }

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

    // ---- Presets (N02, message thread) ----
    PresetManager& getPresetManager() noexcept { return presetManager; }
    // Replaces the four scenes with the preset's as ONE undoable bank edit (published through the normal
    // snapshot handoff) and writes the preset's optional host parameters. False when the index is invalid
    // or a preview is active.
    bool applyPreset(int index);
    // Name of the preset the scenes were last loaded from / saved as; empty when none.
    juce::String getCurrentPresetName() const;
    // True when the four scenes differ from the loaded preset (or no preset is loaded).
    bool isPresetModified() const;
    // Saves the current four scenes (plus the optional host parameters) as a user preset and makes it current.
    PresetManager::Result saveCurrentAsPreset(const juce::String& name, const juce::String& category,
                                              const juce::StringArray& tags, bool overwrite);
    // Marks a preset (e.g. after rename or delete) as the loaded one, or clears it when name is empty.
    void setCurrentPresetName(const juce::String& name);

private:
    // Phrase evolution (F05) plan published with the scene: the audio thread swaps `next` into the evolving
    // scene at the exact phrase boundary (docs/specs/W3-TIMER-EVOLUTION.md).
    struct EvolutionPlan
    {
        bool active = false;
        std::uint8_t scene = 0;
        std::uint8_t every = 1;
        std::uint32_t epoch = 0;    // changes whenever evolution is (re)configured
        std::uint32_t nextStep = 1; // step number `next` represents
        groovemorph::core::VariationSettings next{};
    };
    // Audio-thread cache of the F02 morph result; rebuilt only when its key changes (docs/specs/W3-MORPH.md).
    struct MorphCache
    {
        groovemorph::core::VariationSettings value{};
        bool valid = false;
        std::size_t variation = 0;
        int target = -1;
        float weight = 0.0f;
        std::uint32_t version = 0;
        std::int64_t phraseTicks = 0;
    };
    struct PublishedScene
    {
        groovemorph::core::scene::SceneSnapshot snapshot{};
        bool auditioning = false;
        EvolutionPlan evolution{};
    };

    // Message-thread services at 20 Hz (up to ~50 ms late), started lazily by ensureServices(): host Variation
    // realignment, MIDI macros/links, loop capture publishing, gesture playback and the evolution follow-up.
    // Rate table: docs/specs/W10-MIDI-PANIC-RATES.md.
    ServiceTimer serviceTimer{[this] { serviceTick(); }};
    void serviceTick()
    {
        syncHostVariationParameter();
        applyMidiMacros();
        applyMidiLinks();
        pollLoopCapture();
        updateLoopStretch();
        updateGesture();
        updateEvolution();
        updateSurprise();
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
    void applyScenePhase(const std::optional<groovemorph::core::ResolvedTiming>& blockTiming, int sampleInBlock,
                         const groovemorph::core::control::PhaseConfig& config) noexcept;
    void renderSegment(juce::AudioBuffer<float>& buffer, int numChannels, int start, int length,
                       const std::optional<groovemorph::core::ResolvedTiming>& blockTiming,
                       std::size_t variation, float wetAmount, float bypassTarget) noexcept;
    // Effective settings for a scene: morph blend (F02) applied; placement is applied per slice.
    const groovemorph::core::VariationSettings& effectiveSettings(std::size_t variation) noexcept;
    const groovemorph::core::VariationSettings& morphedSettings(std::size_t variation) noexcept; // F02 result, cached
    groovemorph::core::VariationSettings effectiveScratch{}; // trims/chaos applied on top of the morph result
    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool forceBypass) noexcept;
    // Audio thread: copies the A06 trim parameters into `trims` once per block.
    void readOperationTrims() noexcept;
    // Message thread; caller holds modelMutex. Edits of one numeric field within 500 ms share an undo entry.
    void commitBankCoalescedLocked(const groovemorph::core::scene::SceneBank& next, std::uint64_t key);

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
        std::atomic<float>* bandSlope = nullptr;
        std::atomic<float>* bandTopology = nullptr;
        std::atomic<float>* audioTrigger = nullptr;
        std::atomic<float>* audioTriggerSens = nullptr;
        // A06 trims, operation-major: [op * trimKindCount + kind].
        std::array<std::atomic<float>*, groovemorph::core::operationCount * groovemorph::core::control::trimKindCount>
            operationTrims{};
        std::atomic<float>* chaos = nullptr;
        std::atomic<float>* loopStretch = nullptr;
    } cached;
    bool loadStretchFlag() const noexcept { return cached.loopStretch != nullptr && cached.loopStretch->load(std::memory_order_relaxed) >= 0.5f; }
    juce::AudioParameterBool* bypassParameter = nullptr;

    // Message-thread model.
    mutable std::mutex modelMutex;
    groovemorph::core::scene::UndoStack undoStack;
    PresetManager presetManager;
    std::string currentPresetName;                                  // guarded by modelMutex
    std::optional<groovemorph::core::scene::SceneBank> currentPresetBank; // guarded by modelMutex
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
    float chaosAmount = 0.0f;    // N05 Chaos macro, read once per block; 0 = no overlay
    // F03/F10 band split (first-order complementary crossover) and F07 audio trigger.
    int bandModeSel = 0;          // 0 full band, 1 protect lows, 2 lows only
    float bandCoefficient = 0.0f;
    std::array<float, 2> bandLowState{};
    // 12 dB/oct option: the band sent to the effects is a 2nd-order Butterworth (TPT state variable
    // filter); the bypassed band is its exact complement, so the two always sum back to the input.
    int bandSlopeSel = 0; // 0: 6 dB/oct, 1: 12 dB/oct
    float svfA1 = 0.0f, svfA2 = 0.0f, svfA3 = 0.0f;
    std::array<float, 2> svfIc1{}, svfIc2{};
    juce::AudioBuffer<float> bandBypass;
    juce::AudioBuffer<float> bandPart; // the band handed to the engine, to detect an unchanged signal
    // W3: Linkwitz-Riley split (topology > 0). Classic code above is untouched.
    groovemorph::core::dsp::BandSplit lrSplit;
    int bandTopologySel = 0; // 0 Classic, 1 LR2, 2 LR4
    int previousBandTopologySel = 0;
    std::array<groovemorph::core::OnsetDetector, 2> onsetDetectors;
    int audioTriggerSel = 0;      // 0 off, 1 next scene, 2 trigger queued
    float audioTriggerSensitivity = 0.5f;
    bool onsetThisBlock = false;

    // W3 audio-thread state.
    groovemorph::core::control::LaunchPhase launchPhase; // C04: phase config captured at request time
    float transitionSmoothingMs = 0.0f;                  // C04: smoothing of the sounding scene's launch
    bool dryOverlay = false;                             // C05 Dry return: effects off until a scene request
    std::atomic<float> smoothingStatus{0.0f};
    std::atomic<int> dryReturnStatus{0};                 // 0 none, 1 pending, 2 active
    std::atomic<std::uint32_t> nonFiniteSamples{0};      // input samples replaced by the sanitizer
    MorphCache morphCache;                               // F02 morph result, rebuilt only when its key changes
    std::uint32_t evoEpochSeen = 0;
    std::uint32_t evoAppliedStep = 0;
    std::int64_t evoPhraseSeen = -1;
    bool evoHaveLast = false;                            // evoLast holds the settings of the last applied step
    groovemorph::core::VariationSettings evoLast{};
    // Message thread (modelMutex): evolution epoch; the audio thread reports the last applied step as (epoch << 32 | step).
    std::uint32_t evolutionEpoch = 1;
    std::atomic<std::uint64_t> evolutionApplied{0};
    void syncEvolutionFromAudioLocked();
    bool syncEvolutionStep(); // caller holds modelMutex; true when the audio thread applied a newer step
    void evolutionBoundary(std::int64_t phrase) noexcept;
    void requestScene(std::size_t variation, groovemorph::core::TransitionMode mode) noexcept;

    // UI -> audio.
    std::atomic<int> uiRequest{-1};
    std::atomic<int> uiReset{-1};
    std::atomic<bool> uiResyncParam{false};
    std::atomic<int> expectedParamEcho{-1}; // value the message thread wrote to the host param
    std::atomic<bool> uiTrigger{false};
    std::atomic<bool> uiCancel{false};
    std::atomic<bool> uiRestart{false};
    std::atomic<double> phaseShiftStatus{0.0};
    double phaseShiftBeats = 0.0; // audio thread: pattern position = transport position + shift
    bool phaseNeedsReset = true;  // audio thread: re-resolve the shift for the active scene (Preserve policy)
    double lastPhaseGridBeats = 0.0;
    int previousBandModeSel = 0, previousBandSlopeSel = 0;
    groovemorph::core::control::OperationTrims trims{}; // audio thread: this block's A06 live trims
    bool trimsActive = false;                           // false = every trim at its default (identity)

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
    groovemorph::core::OutputCapture outputCapture;           // N04 print to WAV
    juce::AudioBuffer<float> printScratch;                    // wet-only extraction scratch (prepared with the block size)
    std::atomic<bool> printWetOnly{false};
    std::atomic<bool> printWetFallback{false};                // audio thread: some block was recorded as full output
    juce::String printSceneName;                              // message thread (loopServiceMutex)
    bool printWetOnlyRequested = false;                       // message thread (loopServiceMutex)
    int printBars = 0, printBeatsPerBar = 4;                  // message thread (loopServiceMutex)
    std::atomic<double> loopSeconds{0.0};
    // Message-thread copy of the loop at its sample rate, so it survives prepareToPlay (guarded by loopServiceMutex).
    juce::AudioBuffer<float> loopStore;
    double loopStoreRate = 0.0;
    double loopStoreBeats = 0.0;
    bool republishStoredLoop();
    // Caller holds loopServiceMutex. The stored loop's content (or its published form) changed: invalidate
    // cached peaks/blob, drop stretch jobs and forget the stretch state.
    void loopContentChangedLocked();
    void publishStretchedLoop(const groovemorph::core::StretchWorker::Result& result);
    double loopStoreOrigin = 0.0;
    std::atomic<std::uint64_t> loopGeneration{0};
    groovemorph::core::StretchWorker stretchWorker;
    double stretchRatioInUse = 1.0;      // ratio of the published or in-flight stretched buffer (guarded by loopServiceMutex)
    double stretchPublishedRatio = 1.0;
    std::uint64_t stretchJobId = 0;
    int stretchStableTicks = 0;
    double stretchLastTempo = 0.0;
    bool loopPublishPending = false;      // a finished capture/restored loop is stored but not yet published
    int loopPublishRetries = 0;
    std::atomic<double> loopStretchRatioStatus{1.0};
    std::atomic<bool> loopStretchLimitedStatus{false};
    std::atomic<double> hostTempoStatus{0.0};
    std::atomic<double> loopPlayBeatStatus{0.0};
    std::atomic<int> loopPersistWarning{0};
    std::vector<std::uint8_t> loopBlobCache;
    std::uint64_t loopBlobGeneration = ~0ull;
    mutable std::vector<float> peaksMin, peaksMax;
    mutable std::uint64_t peaksGeneration = ~0ull;
    mutable int peaksColumns = 0;
    std::atomic<double> loopBeatsStatus{0.0};
    std::atomic<double> loopBpmStatus{0.0};
    std::atomic<double> sampleRateStatus{48000.0};
    mutable std::mutex loopServiceMutex; // message-thread loop services vs prepareToPlay; never taken by the audio thread
    std::atomic<double> phraseFractionStatus{0.0};
    std::atomic<std::int64_t> phraseIndexStatus{0};
    mutable std::mutex gestureMutex; // gesture lane state: message thread and host state calls, never the audio thread
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
    // N05 Easy mode (guarded by modelMutex except the atomic).
    std::atomic<bool> easyMode{true};
    bool surprisePending = false;
    std::int64_t surprisePhrase = 0;
    std::uint32_t surpriseAttempt = 0;
    // MIDI links: the audio thread only pushes CC values into the inbox; links are applied by the timer.
    groovemorph::core::CcInbox ccInbox;
    mutable std::mutex midiLinkMutex;
    groovemorph::core::MidiLinkTable midiLinks;
    juce::String midiLearnTarget;
    std::atomic<int> midiLinkNotice{0};
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
