#include "PluginProcessor.h"
#include "core/control/ChaosMapper.h"
#include "core/dsp/LoopImport.h"

#include <limits>
#include <vector>
#include <algorithm>
#include <cmath>

namespace core = groovemorph::core;
namespace scene = groovemorph::core::scene;
namespace variation = groovemorph::core::variation;

namespace
{
constexpr auto stateType = "GrooveMorphState";
constexpr auto sceneTreeType = "SceneBank";
constexpr auto sceneDataProperty = "data";
constexpr auto gestureTreeType = "Gesture";
constexpr auto evolutionTreeType = "Evolution";
constexpr auto midiLinkTreeType = "MidiLinks";
constexpr auto uiTreeType = "Ui";
constexpr int meterDenominators[] = {1, 2, 4, 8, 16};
constexpr int maxProcessedChannels = 2;

float loadParameter(const std::atomic<float>* parameter, float fallback) noexcept
{
    const auto value = parameter != nullptr ? parameter->load(std::memory_order_relaxed) : fallback;
    return std::isfinite(value) ? value : fallback;
}

int loadIndex(const std::atomic<float>* parameter, int maxIndex) noexcept
{
    const auto value = loadParameter(parameter, 0.0f);
    if (!std::isfinite(value))
        return 0;
    return juce::jlimit(0, maxIndex, static_cast<int>(std::lround(juce::jlimit(0.0f, static_cast<float>(maxIndex), value))));
}
} // namespace

GrooveMorphAudioProcessor::GrooveMorphAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, stateType, createParameterLayout())
{
    cached.masterIntensity = parameters.getRawParameterValue(masterIntensityId);
    cached.dryWet = parameters.getRawParameterValue(dryWetId);
    cached.manualTempo = parameters.getRawParameterValue(manualTempoId);
    cached.manualMeterNumerator = parameters.getRawParameterValue(manualMeterNumeratorId);
    cached.manualMeterDenominator = parameters.getRawParameterValue(manualMeterDenominatorId);
    cached.phraseBars = parameters.getRawParameterValue(phraseBarsId);
    cached.activeVariation = parameters.getRawParameterValue(activeVariationId);
    cached.transitionMode = parameters.getRawParameterValue(transitionModeId);
    cached.timingMode = parameters.getRawParameterValue(timingModeId);
    cached.morphTarget = parameters.getRawParameterValue(morphTargetId);
    cached.morphAmount = parameters.getRawParameterValue(morphAmountId);
    cached.autoReturnBars = parameters.getRawParameterValue(autoReturnBarsId);
    cached.autoReturnTarget = parameters.getRawParameterValue(autoReturnTargetId);
    cached.autoReturnUnit = parameters.getRawParameterValue(autoReturnUnitId);
    cached.bandMode = parameters.getRawParameterValue(bandModeId);
    cached.bandCrossover = parameters.getRawParameterValue(bandCrossoverId);
    cached.bandSlope = parameters.getRawParameterValue(bandSlopeId);
    cached.audioTrigger = parameters.getRawParameterValue(audioTriggerId);
    cached.audioTriggerSens = parameters.getRawParameterValue(audioTriggerSensId);
    for (std::size_t op = 0; op < core::operationCount; ++op)
        for (std::size_t kind = 0; kind < core::control::trimKindCount; ++kind)
            cached.operationTrims[op * core::control::trimKindCount + kind] = parameters.getRawParameterValue(
                juce::String(core::control::operationTrimId(op, static_cast<core::control::TrimKind>(kind))));
    cached.chaos = parameters.getRawParameterValue(chaosId);
    bypassParameter = dynamic_cast<juce::AudioParameterBool*>(parameters.getParameter(bypassId));
    lastSeenVariationParam = loadIndex(cached.activeVariation, static_cast<int>(core::variationCount) - 1);
    {
        const std::lock_guard<std::mutex> lock(modelMutex);
        publishSceneLocked();
    }
    // Message-thread realignment of the host Variation parameter (A02).
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        startTimerHz(20);
}

juce::AudioProcessorValueTreeState::ParameterLayout
GrooveMorphAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // Wet amount of the rhythm transformation: 0 = dry path exactly (GM-FR-005).
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
    // Appended in M2/M4; IDs are stable from here on.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{dryWetId, 1},
        "Dry/Wet",
        juce::NormalisableRange<float>{0.0f, 1.0f},
        1.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{activeVariationId, 1},
        "Variation",
        juce::StringArray{"A", "B", "C", "D"},
        0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{transitionModeId, 1},
        "Transition Mode",
        juce::StringArray{"Immediate", "Next Beat", "Next Bar", "Next Phrase", "Manual"},
        2));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassId, 1}, "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{timingModeId, 1}, "Timing Mode",
        juce::StringArray{"Automatic", "Host PPQ", "Sample Clock"}, 0));
    // F02 scene morph: blend the sounding scene toward another scene's settings.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{morphTargetId, 1}, "Morph Target",
        juce::StringArray{"Off", "A", "B", "C", "D"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{morphAmountId, 1}, "Morph Amount",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.0f));
    // C05 automatic return: 0 = off, otherwise switch back after this many bars.
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{autoReturnBarsId, 1}, "Auto Return Bars", 0, 32, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{autoReturnTargetId, 1}, "Auto Return Target",
        juce::StringArray{"Previous", "A", "B", "C", "D"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{autoReturnUnitId, 1}, "Auto Return Unit",
        juce::StringArray{"Bars", "Phrases"}, 0));
    // F03/F10: restrict the effects to part of the spectrum (first-order crossover, 6 dB/oct).
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{bandModeId, 1}, "Band Mode",
        juce::StringArray{"Full band", "Protect lows", "Lows only"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{bandCrossoverId, 1}, "Crossover",
        juce::NormalisableRange<float>{40.0f, 400.0f, 1.0f, 0.5f}, 120.0f));
    // Steeper crossover for the band sent to the effects (the bypassed band stays its exact complement).
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{bandSlopeId, 1}, "Band Slope",
        juce::StringArray{"6 dB/oct", "12 dB/oct"}, 0));
    // F07: detected input transients request a scene change.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{audioTriggerId, 1}, "Audio Trigger",
        juce::StringArray{"Off", "Next scene", "Trigger queued"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{audioTriggerSensId, 1}, "Trigger Sensitivity",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.5f));
    // A06 live trims of the sounding scene, one set per operation (operationCount driven, so operations
    // appended later get their ids automatically). Defaults are the identity: no effect on the sound.
    for (std::size_t op = 0; op < core::operationCount; ++op)
    {
        using core::control::TrimKind;
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{juce::String(core::control::operationTrimId(op, TrimKind::Enable)), 1},
            juce::String(core::control::operationTrimName(op, TrimKind::Enable)),
            juce::StringArray{"Scene", "Off", "On"}, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{juce::String(core::control::operationTrimId(op, TrimKind::Depth)), 1},
            juce::String(core::control::operationTrimName(op, TrimKind::Depth)),
            juce::NormalisableRange<float>{core::control::trimDepthMin, core::control::trimDepthMax}, 1.0f));
        juce::NormalisableRange<float> sliceRange{core::control::trimSliceMin, core::control::trimSliceMax};
        sliceRange.setSkewForCentre(1.0f); // 1 (no change) sits mid-travel
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{juce::String(core::control::operationTrimId(op, TrimKind::Slice)), 1},
            juce::String(core::control::operationTrimName(op, TrimKind::Slice)), sliceRange, 1.0f));
    }
    // N05 Chaos macro: 0 = the scene exactly as stored; higher = a busier sounding scene (never stored).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{chaosId, 1}, "Chaos",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.0f));
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
    prepared.store(false, std::memory_order_relaxed);
    currentSampleRate = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
    maxBlock = juce::jmax(1, samplesPerBlock);
    engine.prepare(currentSampleRate, maxBlock, maxProcessedChannels);
    engine.setTraceSink(&traceRing);
    dryBuffer.setSize(maxProcessedChannels, maxBlock, false, true, false);
    bandBypass.setSize(maxProcessedChannels, maxBlock, false, true, false);
    bandPart.setSize(maxProcessedChannels, maxBlock, false, true, false);
    {
        const std::lock_guard<std::mutex> lock(loopServiceMutex);
        loopSource.prepare(currentSampleRate);
        loopCapture.prepare(currentSampleRate);
        sampleRateStatus.store(currentSampleRate, std::memory_order_relaxed);
        // The host may call prepareToPlay again (sample rate or buffer size change): keep the user's loop.
        if (!republishStoredLoop())
        {
            loopSeconds.store(0.0, std::memory_order_relaxed);
            loopBeatsStatus.store(0.0, std::memory_order_relaxed);
            loopBpmStatus.store(0.0, std::memory_order_relaxed);
        }
    }
    bandLowState.fill(0.0f);
    svfIc1.fill(0.0f);
    svfIc2.fill(0.0f);
    phaseShiftBeats = 0.0;
    phaseNeedsReset = true;
    lastPhaseGridBeats = 0.0;
    for (auto& d : onsetDetectors)
        d.prepare(currentSampleRate);
    freeRunSample = 0;
    freeRunOrigin = 0;
    freeRunSamplesPerBeat = 0.0L;
    previousHostSamplePosition.reset();
    previousHostPpqPosition.reset();
    previousHostBlockSamples = 0;
    previousHostWasPlaying = false;
    bypassStep = static_cast<float>(1.0 / juce::jmax(1.0, bypassFadeSeconds * currentSampleRate));
    const bool bypassed = bypassParameter != nullptr && bypassParameter->get();
    bypassGain = bypassed ? 1.0f : 0.0f;
    // Transient state only; saved parameters and the active variation are kept. A variation
    // parameter change made before prepare is still applied by the first block.
    scheduler.cancel();
    autoReturn.disarm();
    midiTrigger.reset();
    audioSceneVersion = 0;
    pullScene();
    setLatencySamples(0);
    prepared.store(true, std::memory_order_relaxed);
}

void GrooveMorphAudioProcessor::releaseResources()
{
    prepared.store(false, std::memory_order_relaxed);
}

core::HostTransport GrooveMorphAudioProcessor::readHostTransport() const
{
    core::HostTransport transport;
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

core::control::GridContext GrooveMorphAudioProcessor::getGridContext() const noexcept
{
    core::control::GridContext grid;
    const int resolvedBeats = resolvedBeatsPerBar.load(std::memory_order_relaxed);
    const int resolvedBars = resolvedPhraseBars.load(std::memory_order_relaxed);
    if (prepared.load(std::memory_order_relaxed) && resolvedBeats > 0 && resolvedBars > 0)
    {
        grid.beatsPerBar = resolvedBeats;
        grid.phraseBars = resolvedBars;
    }
    else
    {
        grid.beatsPerBar = static_cast<int>(loadParameter(cached.manualMeterNumerator, 4.0f));
        grid.phraseBars = static_cast<int>(loadParameter(cached.phraseBars, 4.0f));
    }
    grid.beatsPerBar = juce::jlimit(1, 32, grid.beatsPerBar); // matches the audio-side meter limit
    grid.phraseBars = juce::jlimit(1, 64, grid.phraseBars);
    return grid;
}

core::TransitionMode GrooveMorphAudioProcessor::currentTransitionMode() const noexcept
{
    return static_cast<core::TransitionMode>(loadIndex(cached.transitionMode, 4));
}

void GrooveMorphAudioProcessor::requestVariation(std::size_t index) noexcept
{
    if (index < core::variationCount)
        uiRequest.store(static_cast<int>(index), std::memory_order_release);
}

// ---------------------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------------------

void GrooveMorphAudioProcessor::pullScene() noexcept
{
    const auto version = sceneVersion.load(std::memory_order_acquire);
    if (version == audioSceneVersion)
        return;
    const juce::SpinLock::ScopedTryLockType lock(sceneLock);
    if (!lock.isLocked())
        return; // keep the previous snapshot; retry next block
    audioScene = sharedScene;
    audioSceneVersion = version;
}

void GrooveMorphAudioProcessor::handleMidi(const juce::uint8* data, int numBytes,
                                           core::TransitionMode mode) noexcept
{
    if (data == nullptr || numBytes < 2)
        return;
    const int status = data[0] & 0xf0;
    if (status == 0xB0 && numBytes >= 3)
        ccInbox.push(data[1] & 0x7f, data[2] & 0x7f); // for MIDI links / learn; applied on the message thread
    if (status == 0xB0 && numBytes >= 3 && (data[1] == 120 || data[1] == 123))
    {
        // All Sound Off / All Notes Off stand in for the missing note-offs: release the held trigger note.
        if (const auto back = midiTrigger.releaseAll(audioScene.snapshot.midiMap))
            scheduler.request(*back, mode);
        return;
    }
    if (midiMacros.load(std::memory_order_relaxed))
    {
        if (status == 0xC0 && data[1] < core::variationCount) // program change -> scene
        {
            scheduler.request(data[1], mode);
            autoReturn.disarm();
            midiNoteEvents.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (status == 0xB0 && numBytes >= 3 && data[1] >= 20 && data[1] <= 22)
        {
            const auto slot = static_cast<std::size_t>(data[1] - 20);
            ccValue[slot].store(static_cast<float>(data[2] & 0x7f) / 127.0f, std::memory_order_relaxed);
            ccDirty.fetch_or(1u << slot, std::memory_order_release);
            return;
        }
    }
    if (numBytes < 3)
        return;
    const int note = data[1] & 0x7f;
    const int velocity = data[2] & 0x7f;
    std::optional<std::size_t> target;
    if (status == 0x90 && velocity > 0)
        target = midiTrigger.noteOn(audioScene.snapshot.midiMap, note, scheduler.activeVariation());
    else if (status == 0x80 || status == 0x90)
        target = midiTrigger.noteOff(audioScene.snapshot.midiMap, note);
    else
        return;
    midiNoteEvents.fetch_add(1, std::memory_order_relaxed);
    if (target)
        scheduler.request(*target, mode);
}

void GrooveMorphAudioProcessor::applyMidiMacros()
{
    const auto dirty = ccDirty.exchange(0, std::memory_order_acq_rel);
    static constexpr const char* ids[] = {masterIntensityId, dryWetId, morphAmountId};
    for (std::size_t slot = 0; slot < 3; ++slot)
        if ((dirty & (1u << slot)) != 0)
            if (auto* parameter = parameters.getParameter(ids[slot]))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(ccValue[slot].load(std::memory_order_relaxed));
                parameter->endChangeGesture();
            }
}

void GrooveMorphAudioProcessor::beginMidiLearn(const juce::String& target)
{
    if (!core::MidiLinkTable::validTarget(target.toStdString()))
        return;
    ccInbox.discardPending(); // only controller movement from now on counts
    const std::lock_guard<std::mutex> lock(midiLinkMutex);
    midiLearnTarget = target;
}

void GrooveMorphAudioProcessor::cancelMidiLearn()
{
    const std::lock_guard<std::mutex> lock(midiLinkMutex);
    midiLearnTarget = {};
}

juce::String GrooveMorphAudioProcessor::getMidiLearnTarget() const
{
    const std::lock_guard<std::mutex> lock(midiLinkMutex);
    return midiLearnTarget;
}

int GrooveMorphAudioProcessor::getMidiLinkCc(const juce::String& target) const
{
    const std::lock_guard<std::mutex> lock(midiLinkMutex);
    return midiLinks.ccFor(target.toStdString());
}

bool GrooveMorphAudioProcessor::linkMidi(int cc, const juce::String& target)
{
    const std::lock_guard<std::mutex> lock(midiLinkMutex);
    const auto result = midiLinks.linkChecked(cc, target.toStdString());
    if (result == core::MidiLinkTable::LinkResult::TableFull)
        midiLinkNotice.store(midiLinkNoticeTableFull, std::memory_order_relaxed);
    return result == core::MidiLinkTable::LinkResult::Ok;
}

int GrooveMorphAudioProcessor::getMidiLinkCount() const
{
    const std::lock_guard<std::mutex> lock(midiLinkMutex);
    return static_cast<int>(midiLinks.links().size());
}

void GrooveMorphAudioProcessor::unlinkMidi(const juce::String& target)
{
    const std::lock_guard<std::mutex> lock(midiLinkMutex);
    midiLinks.unlink(target.toStdString());
}

void GrooveMorphAudioProcessor::clearMidiLinks()
{
    const std::lock_guard<std::mutex> lock(midiLinkMutex);
    midiLinks.clear();
    midiLearnTarget = {};
}

void GrooveMorphAudioProcessor::applyMidiLinks()
{
    core::CcInbox::Change changes[64];
    const auto count = ccInbox.collect(changes, 64);
    if (count == 0)
        return;
    std::vector<core::MidiLink> links;
    {
        const std::lock_guard<std::mutex> lock(midiLinkMutex);
        if (midiLearnTarget.isNotEmpty())
        {
            // Learn: the controller that moved most in this tick is linked to the waiting control and is not
            // applied this time. A tick whose movement is not attributable yet (-1) is retried on the next one.
            const int cc = core::pickLearnCc(changes, count);
            if (cc < 0)
                return;
            const auto result = midiLinks.linkChecked(cc, midiLearnTarget.toStdString());
            midiLearnTarget = {};
            if (result == core::MidiLinkTable::LinkResult::TableFull)
                midiLinkNotice.store(midiLinkNoticeTableFull, std::memory_order_relaxed);
            return;
        }
        links = midiLinks.links();
    }
    for (std::size_t i = 0; i < count; ++i)
        for (const auto& link : links)
            if (link.cc == changes[i].cc)
                applyLinkedValue(link.target, static_cast<float>(changes[i].value) / 127.0f);
}

void GrooveMorphAudioProcessor::applyLinkedValue(const std::string& target, float normalised)
{
    if (target.starts_with("p:"))
    {
        if (auto* parameter = parameters.getParameter(juce::String(target.substr(2))))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(normalised);
            parameter->endChangeGesture();
        }
        return;
    }
    if (target.starts_with("s:") && target.size() > 4)
    {
        const auto effect = static_cast<std::size_t>(target[2] - '0');
        if (effect >= core::operationCount)
            return;
        const auto field = target.substr(4);
        const auto sounding = static_cast<std::size_t>(activeVariation.load(std::memory_order_relaxed));
        auto settings = getSceneBank().variation(std::min(sounding, core::variationCount - 1)).settings.operations[effect];
        if (field == "depth")
            settings.depth = normalised;
        else if (field == "slice")
            settings.sliceBeats = 0.0625f * std::pow(64.0f, normalised); // 1/16 .. 4 beats, logarithmic
        else if (field == "enabled")
            settings.enabled = normalised >= 0.5f;
        setOperationSettingsLive(std::min(sounding, core::variationCount - 1), static_cast<core::OperationType>(effect), settings);
    }
}

void GrooveMorphAudioProcessor::setOperationSettingsLive(std::size_t index, core::OperationType op,
                                                         const core::OperationSettings& settings)
{
    if (index >= core::variationCount || op >= core::OperationType::Count)
        return;
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive())
        return;
    auto bank = undoStack.current();
    auto variation = bank.variation(index).settings;
    const auto locks = variation.control.permissions.operationLocks[static_cast<std::size_t>(op)];
    const auto before = variation;
    variation[op] = settings;
    core::control::enforcePermissions(before, variation, false); // locks apply to controllers too
    (void) locks;
    bank.setVariationSettings(index, variation);
    if (bank == undoStack.current())
        return;
    undoStack.amend(bank);
    publishSceneLocked();
}

core::VariationSettings GrooveMorphAudioProcessor::effectiveSettings(std::size_t variation) const noexcept
{
    const auto index = std::min(variation, core::variationCount - 1);
    const auto& base = audioScene.snapshot.variations[index];
    core::VariationSettings settings = base;
    if (morphTargetIndex >= 0 && morphWeight > 0.0f && static_cast<std::size_t>(morphTargetIndex) != index)
        settings = core::control::morph(base, audioScene.snapshot.variations[static_cast<std::size_t>(morphTargetIndex)],
                                        morphWeight);
    if (trimsActive) // A06: default trims leave the result exactly as before
        core::control::applyTrims(settings, trims);
    // N05 Chaos: a transient overlay of the sounding scene, like the morph blend. Never stored; 0 = untouched.
    if (chaosAmount > 0.0f)
    {
        core::control::ChaosContext chaos;
        chaos.chaos = chaosAmount;
        chaos.seed = audioScene.snapshot.seed;
        chaos.sceneIndex = static_cast<std::uint32_t>(index);
        chaos.phraseTicks = getGridContext().phraseTicks();
        settings = core::control::applyChaos(settings, chaos);
    }
    return settings;
}

void GrooveMorphAudioProcessor::readOperationTrims() noexcept
{
    for (std::size_t op = 0; op < core::operationCount; ++op)
    {
        const auto base = op * core::control::trimKindCount;
        auto& trim = trims[op];
        trim.enable = static_cast<core::control::EnableTrim>(
            loadIndex(cached.operationTrims[base + static_cast<std::size_t>(core::control::TrimKind::Enable)], 2));
        trim.depth = loadParameter(cached.operationTrims[base + static_cast<std::size_t>(core::control::TrimKind::Depth)], 1.0f);
        trim.slice = loadParameter(cached.operationTrims[base + static_cast<std::size_t>(core::control::TrimKind::Slice)], 1.0f);
    }
    trimsActive = !core::control::isIdentity(trims);
}

void GrooveMorphAudioProcessor::applyScenePhase(const std::optional<core::ResolvedTiming>& blockTiming,
                                                int sampleInBlock,
                                                const core::control::PhaseConfig& config) noexcept
{
    if (!blockTiming)
        return;
    auto t = *blockTiming;
    t.currentSample += sampleInBlock;
    if (t.ppqPosition)
        *t.ppqPosition += static_cast<double>(sampleInBlock) * t.grid.tempoBpm / (60.0 * t.sampleRate);
    core::control::GridContext grid;
    grid.beatsPerBar = t.grid.meterNumerator;
    grid.phraseBars = t.grid.phraseBars;
    const double position = t.positionInBeats();
    const double newShift = std::isfinite(position) ? core::control::resolvePhaseShift(config, position, grid) : 0.0;
    if (std::abs(newShift - phaseShiftBeats) > 1.0e-9)
        engine.startTraceEpoch(); // the pattern beat axis jumps: visual consumers need monotonic beats per epoch
    phaseShiftBeats = newShift;
}

void GrooveMorphAudioProcessor::renderSegment(juce::AudioBuffer<float>& buffer, int numChannels,
                                              int start, int length,
                                              const std::optional<core::ResolvedTiming>& blockTiming,
                                              std::size_t variation, float wetAmount,
                                              float bypassTarget) noexcept
{
    const auto settings = effectiveSettings(variation);
    const bool placementActive = core::control::isActive(settings.control);
    std::uint32_t enabledMask = 0;
    for (std::size_t i = 0; i < core::operationCount; ++i)
        if (settings.operations[i].enabled)
            enabledMask |= 1u << i;
    std::array<float*, maxProcessedChannels> channels{};

    for (int done = 0; done < length;)
    {
        int n = std::min(maxBlock, length - done);
        const int offset = start + done;
        for (int c = 0; c < numChannels; ++c)
        {
            channels[static_cast<std::size_t>(c)] = buffer.getWritePointer(c, offset);
            // Sanitize once at the processor input boundary so every later path
            // (dry/bypass, band complement, onset detection and history) sees the
            // same finite sample. Keeping the check bounded and in-place is RT safe.
            for (int i = 0; i < n; ++i)
                if (!std::isfinite(channels[static_cast<std::size_t>(c)][i]))
                    channels[static_cast<std::size_t>(c)][i] = 0.0f;
        }

        core::RhythmTiming rhythm;
        core::control::GridContext gridContext;
        double globalBeat = std::numeric_limits<double>::quiet_NaN();
        if (blockTiming)
        {
            auto t = *blockTiming;
            t.currentSample += offset;
            if (t.ppqPosition)
                *t.ppqPosition += static_cast<double>(offset)
                    * t.grid.tempoBpm / (60.0 * t.sampleRate);
            rhythm.samplesPerBeat = t.samplesPerBeat();
            globalBeat = t.positionInBeats();
            rhythm.beatAtBlockStart = globalBeat + phaseShiftBeats; // C04 pattern phase
            rhythm.isPlaying = t.isPlaying
                && (t.mode != core::TimingMode::HostPpq || t.ppqPosition.has_value());
            gridContext.beatsPerBar = t.grid.meterNumerator;
            gridContext.phraseBars = t.grid.phraseBars;
        }

        // C01/C03/C06/F03: operation targets for this musical position. The slice ends at the
        // next placement boundary (nearest sample, at least one sample) so event edges are
        // sample-accurate regardless of host block size or MIDI splits.
        const core::VariationSettings* render = &settings;
        core::VariationSettings gated;
        if (placementActive && rhythm.isPlaying && rhythm.samplesPerBeat > 0.0
            && std::isfinite(rhythm.beatAtBlockStart))
        {
            const auto targets = core::control::evaluate(settings.control, gridContext,
                                                         rhythm.beatAtBlockStart, enabledMask, globalBeat);
            if (targets.gated)
            {
                gated = settings;
                for (std::size_t i = 0; i < core::operationCount; ++i)
                {
                    if (!targets.active[i])
                        gated.operations[i].enabled = false;
                    else
                    {
                        gated.operations[i].depth *= targets.depthScale[i];
                        if (targets.sliceBeats[i] > 0.0f)
                            gated.operations[i].sliceBeats = targets.sliceBeats[i];
                        rhythm.phaseOffsetBeats[i] = targets.phaseOffsetBeats[i];
                    }
                }
                render = &gated;
                const double samples = targets.ticksUntilChange / core::control::ticksPerBeat
                                       * rhythm.samplesPerBeat;
                if (std::isfinite(samples) && samples < static_cast<double>(n))
                    n = std::max(1, static_cast<int>(std::llround(samples)));
            }
        }

        if (rhythm.isPlaying && rhythm.samplesPerBeat > 0.0)
        {
            const int source = sourceMode.load(std::memory_order_relaxed);
            if (source == 1)
                core::DemoSource::render(channels.data(), numChannels, n, globalBeat,
                                         rhythm.samplesPerBeat, currentSampleRate);
            else if (source == 2)
                loopSource.render(channels.data(), numChannels, n, globalBeat, rhythm.samplesPerBeat);
            const auto captureState = loopCapture.state();
            if (captureState == core::LoopCapture::State::Armed || captureState == core::LoopCapture::State::Recording)
            {
                std::array<const float*, maxProcessedChannels> readOnly{};
                for (int c = 0; c < numChannels; ++c)
                    readOnly[static_cast<std::size_t>(c)] = channels[static_cast<std::size_t>(c)];
                loopCapture.process(readOnly.data(), numChannels, n, globalBeat, rhythm.samplesPerBeat, true);
            }
        }
        else
        {
            if (loopCapture.state() == core::LoopCapture::State::Recording)
                loopCapture.abort(); // transport stopped mid-recording
            // Demo/loop sources only play with the transport: when stopped they must be silent, not the
            // live input leaking through while the user believes playback is stopped.
            if (sourceMode.load(std::memory_order_relaxed) != 0)
                for (int c = 0; c < numChannels; ++c)
                    std::fill_n(channels[static_cast<std::size_t>(c)], n, 0.0f);
        }

        if (audioTriggerSel != 0)
        {
            int found = 0;
            for (int c = 0; c < numChannels; ++c)
                found += onsetDetectors[static_cast<std::size_t>(c)].process(
                    channels[static_cast<std::size_t>(c)], n, audioTriggerSensitivity);
            if (found > 0)
                onsetThisBlock = true;
        }

        const bool needDry = bypassGain > 0.0f || bypassTarget > 0.0f;
        if (needDry || bandModeSel != 0) // dryBuffer also keeps the unsplit input for band-split exactness
            for (int c = 0; c < numChannels; ++c)
                dryBuffer.copyFrom(c, 0, buffer, c, offset, n);

        // F03/F10: split into complementary low/high parts (x = low + (x - low)); only one part
        // goes through the effects, the other bypasses and is added back afterwards.
        if (bandModeSel != 0)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                float* x = channels[static_cast<std::size_t>(c)];
                float* bypass = bandBypass.getWritePointer(c);
                float* part = bandPart.getWritePointer(c);
                if (bandSlopeSel == 1)
                {
                    float ic1 = svfIc1[static_cast<std::size_t>(c)], ic2 = svfIc2[static_cast<std::size_t>(c)];
                    if (!std::isfinite(ic1) || !std::isfinite(ic2))
                        ic1 = ic2 = 0.0f;
                    constexpr float k = 1.41421356f;
                    for (int i = 0; i < n; ++i)
                    {
                        const float in = std::isfinite(x[i]) ? x[i] : 0.0f;
                        const float v3 = in - ic2;
                        const float v1 = svfA1 * ic1 + svfA2 * v3;
                        const float v2 = ic2 + svfA2 * ic1 + svfA3 * v3;
                        ic1 = 2.0f * v1 - ic1;
                        ic2 = 2.0f * v2 - ic2;
                        const float low = v2;
                        const float high = in - k * v1 - v2;
                        const float effected = bandModeSel == 1 ? high : low;
                        x[i] = effected;
                        bypass[i] = in - effected; // exact complement
                        part[i] = effected;
                    }
                    svfIc1[static_cast<std::size_t>(c)] = ic1;
                    svfIc2[static_cast<std::size_t>(c)] = ic2;
                    continue;
                }
                float state = bandLowState[static_cast<std::size_t>(c)];
                if (!std::isfinite(state))
                    state = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    // A non-finite sample must not poison the filter state for every later block.
                    const float in = std::isfinite(x[i]) ? x[i] : 0.0f;
                    state += bandCoefficient * (in - state);
                    const float low = state;
                    const float high = in - low;
                    if (bandModeSel == 1) { bypass[i] = low; x[i] = high; }
                    else { bypass[i] = high; x[i] = low; }
                    part[i] = x[i];
                }
                bandLowState[static_cast<std::size_t>(c)] = state;
            }
        }

        engine.process(channels.data(), numChannels, n, *render, wetAmount, rhythm);

        if (bandModeSel != 0)
            for (int c = 0; c < numChannels; ++c)
            {
                float* x = channels[static_cast<std::size_t>(c)];
                const float* bypass = bandBypass.getReadPointer(c);
                const float* part = bandPart.getReadPointer(c);
                const float* original = dryBuffer.getReadPointer(c);
                for (int i = 0; i < n; ++i)
                    x[i] = x[i] == part[i] ? original[i] : x[i] + bypass[i]; // untouched band: exact dry
            }

        if (needDry)
        {
            for (int i = 0; i < n; ++i)
            {
                if (bypassGain < bypassTarget)
                    bypassGain = std::min(bypassTarget, bypassGain + bypassStep);
                else if (bypassGain > bypassTarget)
                    bypassGain = std::max(bypassTarget, bypassGain - bypassStep);
                const float g = bypassGain;
                for (int c = 0; c < numChannels; ++c)
                {
                    float* out = channels[static_cast<std::size_t>(c)];
                    const float dry = dryBuffer.getSample(c, i);
                    if (g >= 1.0f)
                        out[i] = dry;
                    else if (g > 0.0f)
                        out[i] += g * (dry - out[i]);
                }
            }
        }
        done += n;
    }
}

void GrooveMorphAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    process(buffer, midi, false);
}

void GrooveMorphAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,
                                                     juce::MidiBuffer& midi)
{
    process(buffer, midi, true);
}

void GrooveMorphAudioProcessor::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                                        bool forceBypass) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(getTotalNumInputChannels(), getTotalNumOutputChannels(),
                                       buffer.getNumChannels(), maxProcessedChannels);
    for (int channel = juce::jmax(0, numChannels); channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, numSamples);
    if (!engine.isPrepared() || numSamples <= 0 || numChannels <= 0)
        return;

    pullScene();

    // Marshalled UI/state requests, then automation of the variation parameter.
    if (const int reset = uiReset.exchange(-1, std::memory_order_acq_rel); reset >= 0)
    {
        scheduler.reset(static_cast<std::size_t>(reset));
        midiTrigger.reset();
        phaseNeedsReset = true;
    }
    const int variationParam = loadIndex(cached.activeVariation, static_cast<int>(core::variationCount) - 1);
    if (uiResyncParam.exchange(false, std::memory_order_acq_rel))
        lastSeenVariationParam = variationParam;
    const auto mode = currentTransitionMode();
    if (uiCancel.exchange(false, std::memory_order_acq_rel))
    {
        scheduler.cancel();
        autoReturn.disarm();
    }
    if (variationParam != lastSeenVariationParam)
    {
        lastSeenVariationParam = variationParam;
        // A change written by syncHostVariationParameter() only realigns the displayed value
        // with the sounding variation; it is not a new request (A02).
        if (expectedParamEcho.exchange(-1, std::memory_order_acq_rel) != variationParam)
        {
            scheduler.request(static_cast<std::size_t>(variationParam), mode);
            autoReturn.disarm(); // a deliberate request supersedes any pending automatic return
        }
    }
    paramSeenByAudio.store(variationParam, std::memory_order_release);
    readOperationTrims();
    {
        bandModeSel = loadIndex(cached.bandMode, 2);
        const float crossover = juce::jlimit(20.0f, 1000.0f, loadParameter(cached.bandCrossover, 120.0f));
        bandCoefficient = static_cast<float>(1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi
                                                            * static_cast<double>(crossover) / currentSampleRate));
        bandSlopeSel = loadIndex(cached.bandSlope, 1);
        if (bandModeSel != previousBandModeSel || bandSlopeSel != previousBandSlopeSel)
        {
            // Filter states only advance while their own mode/slope is active; stale ones would click.
            previousBandModeSel = bandModeSel;
            previousBandSlopeSel = bandSlopeSel;
            bandLowState.fill(0.0f);
            svfIc1.fill(0.0f);
            svfIc2.fill(0.0f);
        }
        {
            // Zavalishin/Cytomic TPT state variable filter, Butterworth (k = sqrt 2).
            const double g = std::tan(juce::MathConstants<double>::pi * static_cast<double>(crossover)
                                      / currentSampleRate);
            const double k = std::sqrt(2.0);
            const double a1 = 1.0 / (1.0 + g * (g + k));
            svfA1 = static_cast<float>(a1);
            svfA2 = static_cast<float>(g * a1);
            svfA3 = static_cast<float>(g * g * a1);
        }
        {
            const int previousTrigger = audioTriggerSel;
            audioTriggerSel = loadIndex(cached.audioTrigger, 2);
            if (previousTrigger == 0 && audioTriggerSel != 0)
                for (auto& d : onsetDetectors)
                    d.reset(); // stale envelopes from before would fire a false onset or hide a real one
        }
        audioTriggerSensitivity = juce::jlimit(0.0f, 1.0f, loadParameter(cached.audioTriggerSens, 0.5f));
        if (onsetThisBlock)
        {
            onsetThisBlock = false;
            onsetCount.fetch_add(1, std::memory_order_relaxed);
            if (audioTriggerSel == 1)
            {
                scheduler.request((scheduler.activeVariation() + 1) % core::variationCount, currentTransitionMode());
                autoReturn.disarm();
            }
            else if (audioTriggerSel == 2)
                scheduler.trigger();
        }
        const int morphIdx = loadIndex(cached.morphTarget, 4) - 1;
        morphTargetIndex = morphIdx;
        morphWeight = juce::jlimit(0.0f, 1.0f, loadParameter(cached.morphAmount, 0.0f));
        chaosAmount = juce::jlimit(0.0f, 1.0f, loadParameter(cached.chaos, 0.0f));
    }
    if (const int request = uiRequest.exchange(-1, std::memory_order_acq_rel); request >= 0)
    {
        scheduler.request(static_cast<std::size_t>(request), mode);
        autoReturn.disarm();
    }
    if (uiTrigger.exchange(false, std::memory_order_acq_rel))
        scheduler.trigger();

    // Timing at the block start.
    const auto denominatorIndex = loadIndex(cached.manualMeterDenominator, 4);
    const core::PhraseGrid manualGrid{
        static_cast<double>(loadParameter(cached.manualTempo, 120.0f)),
        static_cast<int>(loadParameter(cached.manualMeterNumerator, 4.0f)),
        meterDenominators[denominatorIndex],
        static_cast<int>(loadParameter(cached.phraseBars, 4.0f)),
        0};
    // The JUCE Standalone wrapper's play head reports only a sample counter, and
    // AudioPlayHead::PositionInfo::getIsPlaying() is then false, which would look like a host
    // that is explicitly stopped and bypass every effect forever. In Standalone, ignore the
    // wrapper's transport and let Automatic mean the free-running Sample Clock.
    const bool standaloneWrapper = wrapperType == wrapperType_Standalone;
    const auto hostTransport = standaloneWrapper ? core::HostTransport{} : readHostTransport();
    auto timingModeSelection = static_cast<core::TimingMode>(loadIndex(cached.timingMode, 2));
    if (timingModeSelection == core::TimingMode::Automatic && standaloneWrapper)
        timingModeSelection = core::TimingMode::SampleClock;

    // Internal clock: when the tempo or meter changes, move the grid origin so the musical position
    // (in beats) is unchanged. Recomputing position as samples x new tempo would make it jump.
    if (internalRewind.exchange(false, std::memory_order_relaxed))
    {
        freeRunSample = 0;
        freeRunOrigin = 0;
        freeRunSamplesPerBeat = 0.0L;
        engine.resetHistory();
        phaseNeedsReset = true; // a rewound clock restarts at the Preserve phase
        autoReturn.disarm();
    }
    auto internalGrid = manualGrid;
    // Samples per beat of the grid the resolver will actually use: host tempo/meter when reported and valid.
    auto effectiveGrid = manualGrid;
    if (hostTransport.tempoBpm)
    {
        auto candidate = effectiveGrid;
        candidate.tempoBpm = *hostTransport.tempoBpm;
        if (core::PhraseClock::samplesPerMeterBeat(currentSampleRate, candidate))
            effectiveGrid = candidate;
    }
    if (hostTransport.meterNumerator && hostTransport.meterDenominator)
    {
        auto candidate = effectiveGrid;
        candidate.meterNumerator = *hostTransport.meterNumerator;
        candidate.meterDenominator = *hostTransport.meterDenominator;
        if (core::PhraseClock::samplesPerMeterBeat(currentSampleRate, candidate))
            effectiveGrid = candidate;
    }
    if (const auto spb = core::PhraseClock::samplesPerMeterBeat(currentSampleRate, effectiveGrid); spb && *spb > 0.0L)
    {
        if (freeRunSamplesPerBeat > 0.0L && *spb != freeRunSamplesPerBeat)
        {
            // Re-base against the counter the resolver will use: a host sample position (Sample Clock mode
            // with automated tempo) or the free-run counter. Either way the musical position stays continuous.
            const long double counter = static_cast<long double>(hostTransport.timeInSamples.value_or(freeRunSample));
            const long double beats = (counter - static_cast<long double>(freeRunOrigin)) / freeRunSamplesPerBeat;
            freeRunOrigin = static_cast<std::int64_t>(std::llround(counter - beats * *spb));
        }
        freeRunSamplesPerBeat = *spb;
    }
    internalGrid.originSample = freeRunOrigin;
    auto timing = core::TransportResolver::resolve(
        hostTransport, internalGrid, freeRunSample, currentSampleRate, timingModeSelection);
    // Play/Stop buttons decide the transport only when no host reports a play state.
    const bool internalDecides = timing && !hostTransport.isPlaying.has_value()
                                 && timing->mode == core::TimingMode::SampleClock;
    internalClockActive.store(internalDecides, std::memory_order_relaxed);
    if (internalDecides)
        timing->isPlaying = internalPlaying.load(std::memory_order_relaxed);

    // Sample position is authoritative for detecting host seeks/loop wraps. For
    // PPQ-only hosts compare against the prior block's tempo, and suspend this
    // check at tempo changes because a ramp may integrate differently in the host.
    // Use wide arithmetic so a hostile sample counter cannot overflow subtraction.
    bool hostDiscontinuity = false;
    if (hostTransport.timeInSamples && previousHostSamplePosition)
    {
        const long double advance = static_cast<long double>(*hostTransport.timeInSamples)
                                    - static_cast<long double>(*previousHostSamplePosition);
        const int expected = previousHostWasPlaying ? previousHostBlockSamples : 0;
        hostDiscontinuity = std::abs(advance - expected) > 1.0L;
    }
    else if (timing && timing->ppqPosition && previousHostPpqPosition
             && timing->grid.tempoBpm == previousHostTempo)
    {
        const double expected = previousHostWasPlaying
            ? previousHostBlockSamples * previousHostTempo / (60.0 * currentSampleRate) : 0.0;
        const double tolerance = std::max(1.0e-7, 2.0 * previousHostTempo / (60.0 * currentSampleRate));
        hostDiscontinuity = std::abs(*timing->ppqPosition - *previousHostPpqPosition - expected) > tolerance;
    }
    if (hostDiscontinuity)
    {
        engine.resetHistory();
        phaseNeedsReset = true; // C04: a seek/loop returns to the active scene's own (Preserve) phase
        autoReturn.disarm(); // a seek/loop cancels a transient automatic return
        if (loopCapture.state() == core::LoopCapture::State::Recording)
            loopCapture.abort(); // a splice of two timeline positions would be a corrupted loop
    }
    previousHostSamplePosition = hostTransport.timeInSamples;
    previousHostPpqPosition = timing ? timing->ppqPosition : std::nullopt;
    previousHostTempo = timing ? timing->grid.tempoBpm : previousHostTempo;
    previousHostWasPlaying = hostTransport.isPlaying.value_or(false);
    previousHostBlockSamples = numSamples;
    timingValid.store(timing.has_value(), std::memory_order_relaxed);
    if (timing)
    {
        resolvedBeatsPerBar.store(timing->grid.meterNumerator, std::memory_order_relaxed);
        resolvedPhraseBars.store(timing->grid.phraseBars, std::memory_order_relaxed);
        timingSource.store(static_cast<int>(timing->source), std::memory_order_relaxed);
        missingTimingFields.store(timing->missingFields, std::memory_order_relaxed);
        transportPlaying.store(timing->isPlaying, std::memory_order_relaxed);
        barInPhrase.store(timing->barInPhrase(), std::memory_order_relaxed);
        phraseFractionStatus.store(timing->barInPhrase() / std::max(1, timing->grid.phraseBars),
                                   std::memory_order_relaxed);
        if (const double phrases = timing->positionInPhrases(); std::isfinite(phrases) && std::abs(phrases) < 1.0e12)
            phraseIndexStatus.store(static_cast<std::int64_t>(std::floor(phrases)), std::memory_order_relaxed);
        beatInBar.store(timing->beatInBar(), std::memory_order_relaxed);
    }
    else
    {
        transportPlaying.store(false, std::memory_order_relaxed); // never leave a stale "playing" when timing is unusable
    }

    const float intensity = juce::jlimit(0.0f, 1.0f, loadParameter(cached.masterIntensity, 0.0f));
    const float dryWet = juce::jlimit(0.0f, 1.0f, loadParameter(cached.dryWet, 1.0f));
    const float wetAmount = intensity * dryWet;
    const bool bypassed = forceBypass || (bypassParameter != nullptr && bypassParameter->get());
    const float bypassTarget = bypassed ? 1.0f : 0.0f;

    const auto playing = [this]() noexcept {
        return audioScene.auditioning ? audioScene.snapshot.activeIndex : scheduler.activeVariation();
    };

    // C05: drop a pending return whose request was cancelled/replaced; otherwise ask the
    // Scheduler (NextBar) during the bar before the return boundary so it lands exactly on it.
    const int autoReturnCount = loadIndex(cached.autoReturnBars, 32);
    const int autoReturnBars = autoReturnCount
        * (loadIndex(cached.autoReturnUnit, 1) == 1 && timing ? std::max(1, timing->grid.phraseBars) : 1);
    if (autoReturnCount == 0)
        autoReturn.disarm();
    if (autoReturn.requestPending())
    {
        const auto queuedNow = scheduler.queuedVariation();
        if (!queuedNow || autoReturn.target() != queuedNow)
            autoReturn.disarm();
    }
    if (timing && timing->isPlaying && autoReturn.armed())
    {
        if (const auto back = autoReturn.poll(timing->positionInBars()))
            scheduler.request(*back, core::TransitionMode::NextBar);
    }

    // A meter or phrase-length change invalidates the shift (it was computed for the old phrase length).
    if (timing)
    {
        const double phraseBeats = static_cast<double>(timing->grid.meterNumerator) * timing->grid.phraseBars;
        if (phraseBeats != lastPhaseGridBeats)
        {
            lastPhaseGridBeats = phraseBeats;
            phaseNeedsReset = true;
        }
    }
    if (phaseNeedsReset && timing)
    {
        phaseNeedsReset = false;
        // Reset to the active scene's own offset with the Preserve policy at this position.
        auto preserve = effectiveSettings(scheduler.activeVariation()).control.phase;
        preserve.policy = core::control::PhasePolicy::Preserve;
        applyScenePhase(timing, 0, preserve);
    }
    if (timing && uiRestart.exchange(false, std::memory_order_acq_rel) && !audioScene.auditioning)
    {
        auto restart = effectiveSettings(scheduler.activeVariation()).control.phase;
        restart.policy = core::control::PhasePolicy::Restart;
        applyScenePhase(timing, 0, restart);
        engine.resetHistory(); // a restarted pattern must not replay audio from before the restart
    }

    // Split the block at MIDI events and at the scheduled switch offset.
    auto event = midi.cbegin();
    const auto eventsEnd = midi.cend();
    int position = 0;
    int lastSegmentStart = 0;
    while (position < numSamples)
    {
        for (; event != eventsEnd && (*event).samplePosition <= position; ++event)
            handleMidi((*event).data, (*event).numBytes, mode);

        int segmentEnd = numSamples;
        if (event != eventsEnd)
            segmentEnd = juce::jlimit(position + 1, numSamples, (*event).samplePosition);
        const int length = segmentEnd - position;
        lastSegmentStart = position;

        if (timing)
        {
            auto segmentTiming = *timing;
            segmentTiming.currentSample += position;
            // MIDI splits the host block. Advance its musical position to the
            // exact segment start before quantizing a variation switch.
            if (segmentTiming.ppqPosition)
                *segmentTiming.ppqPosition += static_cast<double>(position)
                    * segmentTiming.grid.tempoBpm / (60.0 * segmentTiming.sampleRate);
            const auto before = playing();
            const auto beforeActive = scheduler.activeVariation();
            const auto switchEvent = scheduler.process(segmentTiming, length);
            if (switchEvent.switched)
            {
                if (autoReturn.requestPending() && autoReturn.target() == switchEvent.variation)
                    autoReturn.disarm(); // the return itself completed
                else if (autoReturnBars > 0 && !audioScene.auditioning)
                {
                    const int sel = loadIndex(cached.autoReturnTarget, 4);
                    const std::size_t back = sel == 0 ? beforeActive : static_cast<std::size_t>(sel - 1);
                    if (back != switchEvent.variation)
                        autoReturn.arm(segmentTiming.positionInBars()
                                           + static_cast<double>(switchEvent.sampleOffset)
                                                 / std::max(1.0, segmentTiming.samplesPerBar()),
                                       autoReturnBars, back);
                    else
                        autoReturn.disarm();
                }
                else
                    autoReturn.disarm();
                const int offset = static_cast<int>(
                    std::clamp<std::int64_t>(switchEvent.sampleOffset, 0, length - 1));
                if (offset > 0)
                    renderSegment(buffer, numChannels, position, offset, timing, before, wetAmount, bypassTarget);
                // C04: the old scene finishes at its own phase; the destination chooses where its pattern starts.
                applyScenePhase(timing, position + offset, effectiveSettings(switchEvent.variation).control.phase);
                lastSwitchSample.store(segmentTiming.currentSample + offset, std::memory_order_relaxed);
                renderSegment(buffer, numChannels, position + offset, length - offset, timing, playing(),
                              wetAmount, bypassTarget);
            }
            else
            {
                renderSegment(buffer, numChannels, position, length, timing, before, wetAmount, bypassTarget);
            }
        }
        else
        {
            renderSegment(buffer, numChannels, position, length, timing, playing(), wetAmount, bypassTarget);
        }
        position = segmentEnd;
    }
    for (; event != eventsEnd; ++event) // events stamped beyond the block
        handleMidi((*event).data, (*event).numBytes, mode);

    if (!internalDecides || internalPlaying.load(std::memory_order_relaxed))
        freeRunSample += numSamples; // a stopped internal transport holds its position

    autoReturnTargetStatus.store(autoReturn.armed() ? static_cast<int>(*autoReturn.target()) : -1,
                                 std::memory_order_relaxed);
    phaseShiftStatus.store(phaseShiftBeats, std::memory_order_relaxed);
    activeVariation.store(scheduler.activeVariation(), std::memory_order_relaxed);
    const auto queued = scheduler.queuedVariation();
    queuedVariation.store(queued ? static_cast<int>(*queued) : -1, std::memory_order_relaxed);
    const auto until = scheduler.samplesUntilSwitch();
    samplesUntilSwitch.store(
        until ? std::max<std::int64_t>(0, lastSegmentStart + *until - numSamples) : -1,
        std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------------------
// Message thread: scene model
// ---------------------------------------------------------------------------------------

void GrooveMorphAudioProcessor::syncHostVariationParameter()
{
    if (!prepared.load(std::memory_order_relaxed))
        return;
    auto* parameter = parameters.getParameter(activeVariationId);
    if (parameter == nullptr)
        return;
    // A token the audio thread never consumed (the value it saw did not change) is stale; drop it so
    // it cannot swallow a later genuine automation write of the same value.
    if (int token = expectedParamEcho.load(std::memory_order_acquire);
        token >= 0 && paramSeenByAudio.load(std::memory_order_acquire) == token)
        expectedParamEcho.compare_exchange_strong(token, -1, std::memory_order_acq_rel);
    const int active = static_cast<int>(activeVariation.load(std::memory_order_relaxed));
    const int maxIndex = static_cast<int>(core::variationCount) - 1;
    const int shown = juce::jlimit(0, maxIndex,
        static_cast<int>(std::lround(parameter->convertFrom0to1(parameter->getValue()))));
    // Leave the parameter alone while a request is pending, not yet consumed by the audio thread
    // (the audio thread reports the value it last read), or already aligned.
    if (shown == active || queuedVariation.load(std::memory_order_relaxed) >= 0
        || uiRequest.load(std::memory_order_acquire) >= 0
        || paramSeenByAudio.load(std::memory_order_acquire) != shown)
        return;
    expectedParamEcho.store(active, std::memory_order_release);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(active)));
}

void GrooveMorphAudioProcessor::publishSceneLocked()
{
    PublishedScene next;
    next.auditioning = audition.isActive();
    next.snapshot = next.auditioning ? audition.audibleSnapshot(undoStack.current())
                                     : undoStack.current().snapshot();
    if (evolutionOn && !next.auditioning && evolutionStep > 0)
    {
        auto settings = evolutionSettings;
        settings.candidate = evolutionStep - 1;
        next.snapshot.variations[evolutionScene] = variation::buildCandidate(
            undoStack.current().variation(evolutionScene).settings, settings).settings;
    }
    {
        const juce::SpinLock::ScopedLockType lock(sceneLock);
        sharedScene = next;
    }
    sceneVersion.fetch_add(1, std::memory_order_acq_rel);
}

void GrooveMorphAudioProcessor::commitBankLocked(const scene::SceneBank& next)
{
    undoStack.commit(next);
    publishSceneLocked();
}

void GrooveMorphAudioProcessor::commitBankCoalescedLocked(const scene::SceneBank& next, std::uint64_t key)
{
    undoStack.commitCoalesced(next, key, static_cast<std::int64_t>(juce::Time::getMillisecondCounterHiRes()),
                              scene::undoCoalesceWindowMs);
    publishSceneLocked();
}

scene::SceneBank GrooveMorphAudioProcessor::getSceneBank() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return undoStack.current();
}

void GrooveMorphAudioProcessor::setVariationSettings(std::size_t index, const core::VariationSettings& settings)
{
    if (index >= core::variationCount)
        return;
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive())
        return; // a preview owns the edited scene; committing would silently overwrite this edit
    auto bank = undoStack.current();
    const auto before = bank.variation(index).settings;
    bank.setVariationSettings(index, settings);
    commitBankCoalescedLocked(bank, scene::coalesceKey(index, before, bank.variation(index).settings));
}

// Whole-scene operations keep the destination's locked fields and lock configuration (C03).
static void keepDestinationLocks(scene::SceneBank& bank, std::size_t destination,
                                 const core::VariationSettings& before)
{
    auto settings = bank.variation(destination).settings;
    core::control::enforcePermissions(before, settings, false);
    bank.setVariationSettings(destination, settings);
}

bool GrooveMorphAudioProcessor::copyVariationSettings(std::size_t source, std::size_t destination)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive())
        return false;
    auto bank = undoStack.current();
    const auto before = bank.variation(destination < core::variationCount ? destination : 0).settings;
    if (!bank.copyVariationSettings(source, destination))
        return false;
    keepDestinationLocks(bank, destination, before);
    if (bank == undoStack.current())
        return false;
    commitBankLocked(bank);
    return true;
}

bool GrooveMorphAudioProcessor::duplicateVariation(std::size_t source, std::size_t destination)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive())
        return false;
    auto bank = undoStack.current();
    const auto before = bank.variation(destination < core::variationCount ? destination : 0).settings;
    if (!bank.duplicateVariation(source, destination))
        return false;
    keepDestinationLocks(bank, destination, before);
    if (bank == undoStack.current())
        return false;
    commitBankLocked(bank);
    return true;
}

void GrooveMorphAudioProcessor::resetVariation(std::size_t index)
{
    if (index >= core::variationCount)
        return;
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive())
        return;
    auto bank = undoStack.current();
    const auto before = bank.variation(index).settings;
    bank.resetVariation(index);
    keepDestinationLocks(bank, index, before);
    commitBankLocked(bank);
}

void GrooveMorphAudioProcessor::setOperationSettings(std::size_t index, core::OperationType op,
                                                     const core::OperationSettings& settings)
{
    if (index >= core::variationCount || op >= core::OperationType::Count)
        return;
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive())
        return; // a preview owns the edited scene; committing would silently overwrite this edit
    auto bank = undoStack.current();
    const auto before = bank.variation(index).settings;
    auto variation = before;
    variation[op] = settings;
    bank.setVariationSettings(index, variation);
    commitBankCoalescedLocked(bank, scene::coalesceKey(index, before, bank.variation(index).settings));
}

void GrooveMorphAudioProcessor::setVariationName(std::size_t index, std::string_view name)
{
    if (index >= core::variationCount)
        return;
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive())
        return; // a preview owns the edited scene; committing would silently overwrite this edit
    auto bank = undoStack.current();
    bank.setVariationName(index, name);
    commitBankLocked(bank);
}

void GrooveMorphAudioProcessor::setMidiNoteMap(const scene::MidiNoteMap& map)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive())
        return; // a preview owns the edited scene; committing would silently overwrite this edit
    auto bank = undoStack.current();
    bank.setMidiNoteMap(map);
    commitBankLocked(bank);
}

void GrooveMorphAudioProcessor::setSceneSeed(std::uint64_t seed)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive())
        return; // a preview owns the edited scene; committing would silently overwrite this edit
    auto bank = undoStack.current();
    bank.setSeed(seed);
    commitBankLocked(bank);
}

bool GrooveMorphAudioProcessor::undoSceneEdit()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive() || !undoStack.undo())
        return false;
    publishSceneLocked();
    return true;
}

bool GrooveMorphAudioProcessor::redoSceneEdit()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive() || !undoStack.redo())
        return false;
    publishSceneLocked();
    return true;
}

int GrooveMorphAudioProcessor::getCurrentProgram()
{
    return juce::jmax(0, presetManager.indexOf(getCurrentPresetName()));
}

juce::String GrooveMorphAudioProcessor::getCurrentPresetName() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return juce::String(currentPresetName);
}

bool GrooveMorphAudioProcessor::isPresetModified() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return !currentPresetBank.has_value()
           || !groovemorph::core::preset::sameScenes(undoStack.current(), *currentPresetBank);
}

void GrooveMorphAudioProcessor::setCurrentPresetName(const juce::String& name)
{
    std::optional<groovemorph::core::scene::SceneBank> presetBank;
    std::string resolved;
    if (name.isNotEmpty())
        if (const auto index = presetManager.indexOf(name); index >= 0)
            if (const auto preset = presetManager.presetAt(index))
            {
                resolved = preset->name;
                presetBank = groovemorph::core::preset::bankOf(*preset);
            }
    const std::lock_guard<std::mutex> lock(modelMutex);
    currentPresetName = presetBank ? resolved : std::string();
    currentPresetBank = std::move(presetBank);
}

bool GrooveMorphAudioProcessor::applyPreset(int index)
{
    // Message thread (also reached from host program changes, which hosts issue off the audio thread).
    const auto preset = presetManager.presetAt(index);
    if (!preset)
        return false;
    auto presetBank = groovemorph::core::preset::bankOf(*preset);
    if (!presetBank)
        return false;
    {
        const std::lock_guard<std::mutex> lock(modelMutex);
        if (audition.isActive())
            return false;
        // One undo entry; the audio thread picks the new snapshot up through the usual handoff.
        commitBankLocked(groovemorph::core::preset::applyToBank(undoStack.current(), *presetBank));
        currentPresetName = preset->name;
        currentPresetBank = std::move(presetBank);
    }
    for (const auto& param : preset->params)
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameters.getParameter(juce::String(param.id))))
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, param.value));
            ranged->endChangeGesture();
        }
    return true;
}

PresetManager::Result GrooveMorphAudioProcessor::saveCurrentAsPreset(const juce::String& name, const juce::String& category,
                                                                      const juce::StringArray& tags, bool overwrite)
{
    scene::SceneBank bank;
    {
        const std::lock_guard<std::mutex> lock(modelMutex);
        bank = undoStack.current();
    }
    std::vector<std::string> tagList;
    for (const auto& tag : tags)
        tagList.push_back(tag.toStdString());
    std::vector<groovemorph::core::preset::PresetParam> params;
    for (const char* id : {masterIntensityId, dryWetId, morphTargetId, morphAmountId, bandModeId, bandCrossoverId, bandSlopeId})
        if (auto* parameter = parameters.getParameter(id))
            params.push_back({id, parameter->getValue()});
    auto preset = groovemorph::core::preset::makePreset(name.toStdString(), category.toStdString(), std::move(tagList), bank,
                                                        "", std::move(params));
    auto result = presetManager.saveUser(std::move(preset), overwrite);
    if (result.ok())
        setCurrentPresetName(presetManager.nameAt(result.index));
    return result;
}

bool GrooveMorphAudioProcessor::canUndoSceneEdit() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return undoStack.canUndo();
}

bool GrooveMorphAudioProcessor::canRedoSceneEdit() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return undoStack.canRedo();
}

void GrooveMorphAudioProcessor::beginAudition(std::size_t index)
{
    if (index >= core::variationCount)
        return;
    const std::lock_guard<std::mutex> lock(modelMutex);
    audition.begin(undoStack.current(), index);
    publishSceneLocked();
}

void GrooveMorphAudioProcessor::setAuditionEdit(const scene::Variation& edited)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (!audition.isActive())
        return;
    audition.setEdited(edited);
    publishSceneLocked();
}

void GrooveMorphAudioProcessor::setAuditionComparingStored(bool hearStored)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    audition.setComparingStored(hearStored);
    publishSceneLocked();
}

void GrooveMorphAudioProcessor::toggleAuditionCompare()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    audition.toggleCompare();
    publishSceneLocked();
}

bool GrooveMorphAudioProcessor::isAuditioning() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return audition.isActive();
}

bool GrooveMorphAudioProcessor::isAuditionDirty() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return audition.isActive() && audition.isDirty();
}

bool GrooveMorphAudioProcessor::commitAudition()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    auto bank = undoStack.current();
    if (!audition.commit(bank))
        return false;
    commitBankLocked(bank);
    return true;
}

void GrooveMorphAudioProcessor::revertAudition()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    audition.revert();
    publishSceneLocked();
}

scene::LoadReport GrooveMorphAudioProcessor::getLastSceneLoadReport() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return lastLoadReport;
}

// ---------------------------------------------------------------------------------------
// Host state (GM-FR-010)
// ---------------------------------------------------------------------------------------

void GrooveMorphAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::string sceneText;
    {
        const std::lock_guard<std::mutex> lock(modelMutex);
        auto bank = undoStack.current();
        bank.setActiveIndex(activeVariation.load(std::memory_order_relaxed));
        sceneText = scene::SceneSerializer::serialize(bank);
    }
    juce::ValueTree sceneTree(sceneTreeType);
    sceneTree.setProperty(sceneDataProperty, juce::String::fromUTF8(sceneText.data(),
                                                                    static_cast<int>(sceneText.size())),
                          nullptr);
    {
        // N02: remember which preset the scenes came from (label only; optional, absent in older states).
        const std::lock_guard<std::mutex> lock(modelMutex);
        if (!currentPresetName.empty())
            sceneTree.setProperty("presetName", juce::String(currentPresetName), nullptr);
    }
    state.removeChild(state.getChildWithName(sceneTreeType), nullptr);
    state.appendChild(sceneTree, nullptr);

    // Gesture lane (F11) and phrase evolution (F05) settings travel with the project.
    juce::ValueTree gestureTree(gestureTreeType);
    {
        const std::lock_guard<std::mutex> lock(gestureMutex);
        gestureTree.setProperty("target", gestureTarget, nullptr);
        gestureTree.setProperty("bins", juce::String(gesture.serialize()), nullptr);
    }
    juce::ValueTree evolutionTree(evolutionTreeType);
    {
        const std::lock_guard<std::mutex> lock(modelMutex);
        evolutionTree.setProperty("enabled", evolutionOn, nullptr);
        evolutionTree.setProperty("scene", static_cast<int>(evolutionScene), nullptr);
        evolutionTree.setProperty("every", evolutionEvery, nullptr);
        evolutionTree.setProperty("distance", static_cast<double>(evolutionSettings.distance), nullptr);
        evolutionTree.setProperty("style", static_cast<int>(evolutionSettings.style), nullptr);
    }
    for (const char* type : {gestureTreeType, evolutionTreeType})
        state.removeChild(state.getChildWithName(type), nullptr);
    state.appendChild(gestureTree, nullptr);
    state.appendChild(evolutionTree, nullptr);
    {
        // N05: which editor view was open. Absent in state saved before Easy mode (then the full view opens).
        juce::ValueTree uiTree(uiTreeType);
        uiTree.setProperty("easyMode", isEasyMode(), nullptr);
        state.removeChild(state.getChildWithName(uiTreeType), nullptr);
        state.appendChild(uiTree, nullptr);
    }
    {
        juce::ValueTree linkTree(midiLinkTreeType);
        {
            const std::lock_guard<std::mutex> lock(midiLinkMutex);
            linkTree.setProperty(sceneDataProperty, juce::String(midiLinks.serialize()), nullptr);
        }
        state.removeChild(state.getChildWithName(midiLinkTreeType), nullptr);
        state.appendChild(linkTree, nullptr);
    }
    if (const auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void GrooveMorphAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Limit untrusted host-supplied state before JUCE XML decoding. Preserve the
    // currently loaded scene and parameters if the envelope itself is invalid.
    constexpr int maximumStateBytes = 1024 * 1024;
    if (data == nullptr || sizeInBytes <= 0 || sizeInBytes > maximumStateBytes)
        return;

    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(stateType))
        return;

    auto restoredState = juce::ValueTree::fromXml(*xml);
    if (!restoredState.isValid() || !restoredState.hasType(parameters.state.getType()))
        return;

    std::string sceneText;
    std::string restoredPresetName;
    const auto sceneTree = restoredState.getChildWithName(sceneTreeType);
    if (sceneTree.isValid())
    {
        sceneText = sceneTree.getProperty(sceneDataProperty).toString().toStdString();
        restoredPresetName = sceneTree.getProperty("presetName").toString().toStdString();
        restoredState.removeChild(sceneTree, nullptr);
    }
    // Gesture and evolution are optional (absent in older projects) and parsed defensively.
    const auto gestureTree = restoredState.getChildWithName(gestureTreeType);
    const auto evolutionTree = restoredState.getChildWithName(evolutionTreeType);
    const juce::String gestureBins = gestureTree.isValid() ? gestureTree.getProperty("bins").toString() : juce::String();
    const int gestureTargetRestored = gestureTree.isValid() ? static_cast<int>(gestureTree.getProperty("target", 0)) : 0;
    const bool evolutionRestored = evolutionTree.isValid();
    const bool evolutionEnabled = evolutionRestored && static_cast<bool>(evolutionTree.getProperty("enabled", false));
    const int evolutionSceneRestored = evolutionRestored ? static_cast<int>(evolutionTree.getProperty("scene", 0)) : 0;
    const int evolutionEveryRestored = evolutionRestored ? static_cast<int>(evolutionTree.getProperty("every", 1)) : 1;
    const double evolutionDistance = evolutionRestored ? static_cast<double>(evolutionTree.getProperty("distance", 0.3)) : 0.3;
    const int evolutionStyle = evolutionRestored ? static_cast<int>(evolutionTree.getProperty("style", 0)) : 0;
    for (const char* type : {gestureTreeType, evolutionTreeType})
        if (const auto child = restoredState.getChildWithName(type); child.isValid())
            restoredState.removeChild(child, nullptr);
    // N05: a project saved before Easy mode has no Ui child and keeps opening on the full editor.
    {
        const auto uiTree = restoredState.getChildWithName(uiTreeType);
        setEasyMode(uiTree.isValid() && static_cast<bool>(uiTree.getProperty("easyMode", false)));
        if (uiTree.isValid())
            restoredState.removeChild(uiTree, nullptr);
    }
    // Pre-selector projects always used Automatic. Restore that behavior even
    // when an older preset is loaded into an instance currently using Sample Clock.
    if (!restoredState.getChildWithProperty("id", timingModeId).isValid())
    {
        juce::ValueTree clockState{"PARAM"};
        clockState.setProperty("id", timingModeId, nullptr);
        clockState.setProperty("value", 0.0f, nullptr);
        restoredState.appendChild(clockState, nullptr);
    }
    // Missing or malformed scene data yields defaults plus a recovery report.
    auto result = scene::SceneSerializer::deserialize(sceneText);
    const bool unreadableScenes = !sceneText.empty() && result.report.status == scene::LoadStatus::Defaulted;
    juce::String linkText;
    if (const auto linkTree = restoredState.getChildWithName(midiLinkTreeType); linkTree.isValid())
    {
        linkText = linkTree.getProperty(sceneDataProperty).toString();
        restoredState.removeChild(linkTree, nullptr);
    }
    // Parameters absent from the restored state would otherwise keep the previous session's values.
    // Note them before replacing so they can return to their defaults afterwards.
    std::vector<juce::RangedAudioParameter*> missingParameters;
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            if (!restoredState.getChildWithProperty("id", ranged->paramID).isValid())
                missingParameters.push_back(ranged);
    parameters.replaceState(restoredState);
    for (auto* ranged : missingParameters)
        ranged->setValueNotifyingHost(ranged->getDefaultValue());
    {
        const std::lock_guard<std::mutex> lock(midiLinkMutex);
        midiLinks = core::MidiLinkTable::deserialize(linkText.toStdString());
        midiLearnTarget = {};
    }

    if (unreadableScenes)
    {
        const std::lock_guard<std::mutex> lock(modelMutex);
        lastLoadReport = std::move(result.report);
        lastLoadReport.messages.push_back("current scenes kept");
        publishSceneLocked();
        uiResyncParam.store(true, std::memory_order_release);
        return;
    }
    const auto active = result.bank.activeIndex();
    {
        const std::lock_guard<std::mutex> lock(modelMutex);
        audition.revert();
        undoStack.reset(result.bank);
        lastLoadReport = std::move(result.report);
        publishSceneLocked();
        currentPresetName.clear();
        currentPresetBank.reset();
        if (const auto presetIndex = presetManager.indexOf(juce::String(restoredPresetName)); presetIndex >= 0)
            if (const auto preset = presetManager.presetAt(presetIndex))
                if (auto presetBank = groovemorph::core::preset::bankOf(*preset))
                {
                    currentPresetName = preset->name;
                    currentPresetBank = std::move(presetBank);
                }
    }
    {
        const std::lock_guard<std::mutex> lock(gestureMutex);
        gesture.clear();
        gestureRecording = gesturePlaying = false;
        gestureTarget = juce::jlimit(0, 2, gestureTargetRestored);
        const auto text = gestureBins.toStdString();
        gesture.deserialize(text);
    }
    setEvolution(evolutionEnabled, static_cast<std::size_t>(juce::jmax(0, evolutionSceneRestored)),
                 evolutionEveryRestored, std::isfinite(evolutionDistance) ? static_cast<float>(juce::jlimit(0.0, 1.0, evolutionDistance)) : 0.3f,
                 evolutionStyle, 0x47524F4F56ull);
    activeVariation.store(active, std::memory_order_relaxed);
    queuedVariation.store(-1, std::memory_order_relaxed);
    samplesUntilSwitch.store(-1, std::memory_order_relaxed);
    uiRequest.store(-1, std::memory_order_relaxed);
    uiTrigger.store(false, std::memory_order_relaxed);
    uiResyncParam.store(true, std::memory_order_release);
    uiReset.store(static_cast<int>(active), std::memory_order_release);
}

void GrooveMorphAudioProcessor::resetToDefaults()
{
    // Message thread. Every host parameter back to its default, factory scenes (names, effects, placement,
    // MIDI notes, seed), no preview, no loop, live input, no evolution or gesture, transport at the start.
    for (auto* parameter : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost(ranged->getDefaultValue());
            ranged->endChangeGesture();
        }
    }
    {
        const std::lock_guard<std::mutex> lock(modelMutex);
        audition.revert();
        undoStack.reset(scene::SceneBank{});
        lastLoadReport = {};
        currentPresetName.clear();
        currentPresetBank.reset();
        publishSceneLocked();
    }
    setEvolution(false, 0, 1, 0.5f, 1, evolutionSettings.seed);
    setGestureRecording(false);
    setGesturePlaying(false);
    clearGesture();
    clearLoop();
    clearMidiLinks();
    setSourceMode(0);
    {
        const std::lock_guard<std::mutex> lock(loopServiceMutex);
        loopCapture.cancel();
    }
    internalPlaying.store(true, std::memory_order_relaxed);
    internalRewind.store(true, std::memory_order_relaxed);
    activeVariation.store(0, std::memory_order_relaxed);
    queuedVariation.store(-1, std::memory_order_relaxed);
    samplesUntilSwitch.store(-1, std::memory_order_relaxed);
    uiRequest.store(-1, std::memory_order_relaxed);
    uiTrigger.store(false, std::memory_order_relaxed);
    uiResyncParam.store(true, std::memory_order_release);
    uiReset.store(0, std::memory_order_release);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrooveMorphAudioProcessor();
}

// ---------------------------------------------------------------------------------------
// Alpha 2 message-thread services: loop capture/import (F09), gestures (F11), evolution (F05)
// ---------------------------------------------------------------------------------------

bool GrooveMorphAudioProcessor::armLoopCapture(int bars)
{
    const std::lock_guard<std::mutex> lock(loopServiceMutex);
    return loopCapture.arm(bars, getGridContext().beatsPerBar);
}

void GrooveMorphAudioProcessor::pollLoopCapture()
{
    if (loopCapture.state() != core::LoopCapture::State::Done)
        return;
    const std::lock_guard<std::mutex> lock(loopServiceMutex);
    if (loopCapture.state() != core::LoopCapture::State::Done)
        return;
    const float* channels[2] = {loopCapture.channel(0), loopCapture.channel(1)};
    const auto samples = loopCapture.recordedSamples();
    if (loopSource.load(channels, 2, samples, loopCapture.loopBeats(), loopCapture.startBeat()))
    {
        const double seconds = static_cast<double>(samples) / sampleRateStatus.load(std::memory_order_relaxed);
        loopSeconds.store(seconds, std::memory_order_relaxed);
        loopBeatsStatus.store(loopCapture.loopBeats(), std::memory_order_relaxed);
        loopBpmStatus.store(seconds > 0.0 ? loopCapture.loopBeats() * 60.0 / seconds : 0.0, std::memory_order_relaxed);
        loopStore.setSize(2, static_cast<int>(samples), false, true, false);
        for (int c = 0; c < 2; ++c)
            loopStore.copyFrom(c, 0, channels[c], static_cast<int>(samples));
        loopStoreRate = sampleRateStatus.load(std::memory_order_relaxed);
        loopStoreBeats = loopCapture.loopBeats();
    }
    loopCapture.release();
}

void GrooveMorphAudioProcessor::clearLoop() noexcept
{
    const std::lock_guard<std::mutex> lock(loopServiceMutex);
    loopSource.clear();
    loopStore.setSize(0, 0);
    loopStoreRate = 0.0;
    loopStoreBeats = 0.0;
    loopSeconds.store(0.0, std::memory_order_relaxed);
    loopBeatsStatus.store(0.0, std::memory_order_relaxed);
    loopBpmStatus.store(0.0, std::memory_order_relaxed);
}

// Caller holds loopServiceMutex. Re-publishes the stored loop at the current sample rate. False when none is stored.
bool GrooveMorphAudioProcessor::republishStoredLoop()
{
    if (loopStore.getNumSamples() < 16 || loopStoreRate <= 0.0 || !(loopStoreBeats > 0.0))
        return false;
    const double ratio = loopStoreRate / currentSampleRate;
    const auto outLength = static_cast<int>(std::llround(static_cast<double>(loopStore.getNumSamples()) / ratio));
    if (outLength < 16 || outLength > loopSource.capacitySamples())
    {
        loopStore.setSize(0, 0);
        return false;
    }
    juce::AudioBuffer<float> out(2, outLength);
    for (int c = 0; c < 2; ++c)
    {
        if (std::abs(ratio - 1.0) < 1.0e-9)
            out.copyFrom(c, 0, loopStore, juce::jmin(c, loopStore.getNumChannels() - 1), 0, outLength);
        else
        {
            // The interpolator reads a few samples past the rounded-up end; give it zero padding to read.
            const int sourceChannel = juce::jmin(c, loopStore.getNumChannels() - 1);
            juce::AudioBuffer<float> padded(1, loopStore.getNumSamples() + static_cast<int>(std::ceil(ratio)) + 8);
            padded.clear();
            padded.copyFrom(0, 0, loopStore, sourceChannel, 0, loopStore.getNumSamples());
            juce::LagrangeInterpolator interpolator;
            interpolator.process(ratio, padded.getReadPointer(0), out.getWritePointer(c), outLength);
        }
    }
    const float* channels[2] = {out.getReadPointer(0), out.getReadPointer(1)};
    if (!loopSource.load(channels, 2, outLength, loopStoreBeats, 0.0))
        return false;
    loopStore = out;
    loopStoreRate = currentSampleRate;
    loopSeconds.store(static_cast<double>(outLength) / currentSampleRate, std::memory_order_relaxed);
    return true;
}

juce::Result GrooveMorphAudioProcessor::importLoopFile(const juce::File& file, int bars, double nativeBpm)
{
    const std::lock_guard<std::mutex> lock(loopServiceMutex);
    if (bars < 0 || bars > 16)
        return juce::Result::fail("Bars must be automatic or 1-16");
    if (nativeBpm != 0.0 && !(nativeBpm >= 20.0 && nativeBpm <= 400.0))
        return juce::Result::fail("Loop tempo must be 20-400 BPM (or 0 for automatic)");
    if (!file.existsAsFile())
        return juce::Result::fail("File not found");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->numChannels < 1 || reader->sampleRate <= 0.0)
        return juce::Result::fail("Unsupported or unreadable audio file");
    const double seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    if (seconds < 0.05)
        return juce::Result::fail("File is too short");
    if (seconds > core::LoopSource::maxSeconds)
        return juce::Result::fail("File is longer than 20 seconds");
    const double deviceRate = currentSampleRate > 0.0 ? currentSampleRate : 48000.0;
    const auto outLength = static_cast<int>(std::llround(seconds * deviceRate));
    if (outLength < 16 || outLength > loopSource.capacitySamples())
        return juce::Result::fail("File has too many samples for this session's sample rate; use a shorter file");
    const auto length = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> data(juce::jmin(2, static_cast<int>(reader->numChannels)), length);
    if (!reader->read(&data, 0, length, 0, true, data.getNumChannels() > 1))
        return juce::Result::fail("Could not read the audio data");

    // Resample to the device rate so the loop plays at its true pitch (the beat mapping below only fixes speed).
    juce::AudioBuffer<float> resampled(data.getNumChannels(), outLength);
    if (std::abs(reader->sampleRate - deviceRate) < 0.5)
    {
        for (int c = 0; c < data.getNumChannels(); ++c)
            resampled.copyFrom(c, 0, data, c, 0, juce::jmin(length, outLength));
    }
    else
    {
        for (int c = 0; c < data.getNumChannels(); ++c)
        {
            const double ratio = reader->sampleRate / deviceRate;
            juce::AudioBuffer<float> padded(1, length + static_cast<int>(std::ceil(ratio)) + 8); // zero tail for the interpolator
            padded.clear();
            padded.copyFrom(0, 0, data, c, 0, length);
            juce::LagrangeInterpolator interpolator;
            interpolator.process(ratio, padded.getReadPointer(0), resampled.getWritePointer(c), outLength);
        }
    }

    const auto mapping = core::mapLoop(seconds, getGridContext().beatsPerBar,
                                       static_cast<double>(loadParameter(cached.manualTempo, 120.0f)), bars, nativeBpm);
    if (!mapping.valid)
        return juce::Result::fail("Could not work out the loop length");
    const float* channels[2] = {resampled.getReadPointer(0), resampled.getReadPointer(resampled.getNumChannels() - 1)};
    if (!loopSource.load(channels, 2, outLength, mapping.beats, 0.0))
        return juce::Result::fail("Could not load the loop");
    loopSeconds.store(seconds, std::memory_order_relaxed);
    loopBeatsStatus.store(mapping.beats, std::memory_order_relaxed);
    loopBpmStatus.store(mapping.nativeBpm, std::memory_order_relaxed);
    loopStore = resampled;
    loopStoreRate = deviceRate;
    loopStoreBeats = mapping.beats;
    return juce::Result::ok();
}

bool GrooveMorphAudioProcessor::matchTempoToLoop()
{
    const double bpm = getLoopBpm();
    auto* parameter = parameters.getParameter(manualTempoId);
    if (parameter == nullptr || !hasLoop() || !(bpm > 0.0))
        return false;
    const auto range = parameter->getNormalisableRange();
    if (bpm < range.start || bpm > range.end)
        return false;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(bpm)));
    parameter->endChangeGesture();
    return true;
}

void GrooveMorphAudioProcessor::setGestureTarget(int target)
{
    const std::lock_guard<std::mutex> lock(gestureMutex);
    target = juce::jlimit(0, 2, target);
    if (target != gestureTarget)
    {
        gesture.clear(); // a curve is only meaningful for the parameter it was recorded on
        gestureRecording = gesturePlaying = false;
    }
    gestureTarget = target;
}

GrooveMorphAudioProcessor::EvolutionSettings GrooveMorphAudioProcessor::getEvolutionSettings() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return {evolutionOn, evolutionScene, evolutionEvery, evolutionSettings.distance,
            static_cast<int>(evolutionSettings.style)};
}

void GrooveMorphAudioProcessor::setGestureRecording(bool record)
{
    const std::lock_guard<std::mutex> lock(gestureMutex);
    gestureRecording = record;
    if (record)
        gesturePlaying = false;
}

void GrooveMorphAudioProcessor::setGesturePlaying(bool play)
{
    const std::lock_guard<std::mutex> lock(gestureMutex);
    gesturePlaying = play && gesture.hasData();
    if (gesturePlaying)
        gestureRecording = false;
}

void GrooveMorphAudioProcessor::clearGesture()
{
    const std::lock_guard<std::mutex> lock(gestureMutex);
    gesture.clear();
    gestureRecording = gesturePlaying = false;
}

void GrooveMorphAudioProcessor::updateGesture()
{
    float playbackValue = -1.0f;
    juce::RangedAudioParameter* target = nullptr;
    {
        const std::lock_guard<std::mutex> lock(gestureMutex);
        if ((!gestureRecording && !gesturePlaying) || !transportPlaying.load(std::memory_order_relaxed))
            return;
        static constexpr const char* ids[] = {morphAmountId, masterIntensityId, dryWetId};
        target = parameters.getParameter(ids[gestureTarget]);
        if (target == nullptr)
            return;
        const double fraction = phraseFractionStatus.load(std::memory_order_relaxed);
        if (gestureRecording)
            gesture.record(fraction, target->getValue());
        else
            playbackValue = gesture.sample(fraction);
    }
    // The host is called without holding gestureMutex (a host may save state from this notification).
    // Playback is a stream of plugin-driven values, not a user touch, so it is not a change gesture.
    if (playbackValue >= 0.0f)
        target->setValueNotifyingHost(playbackValue);
}

void GrooveMorphAudioProcessor::setEvolution(bool enabled, std::size_t scene, int everyPhrases, float distance,
                                             int style, std::uint64_t seed)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    evolutionOn = enabled;
    evolutionScene = juce::jmin(scene, core::variationCount - 1);
    evolutionEvery = juce::jlimit(1, 16, everyPhrases);
    evolutionSettings = variation::GeneratorSettings{};
    evolutionSettings.seed = seed;
    evolutionSettings.distance = juce::jlimit(0.0f, 1.0f, distance);
    evolutionSettings.style = static_cast<variation::Style>(juce::jlimit(0, 2, style));
    evolutionStep = 0;
    lastEvolutionPhrase = phraseIndexStatus.load(std::memory_order_relaxed);
    publishSceneLocked();
}

bool GrooveMorphAudioProcessor::isEvolving() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return evolutionOn;
}

std::uint32_t GrooveMorphAudioProcessor::getEvolutionStep() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return evolutionStep;
}

void GrooveMorphAudioProcessor::updateEvolution()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (!evolutionOn || !transportPlaying.load(std::memory_order_relaxed))
        return;
    const auto phrase = phraseIndexStatus.load(std::memory_order_relaxed);
    if (phrase == lastEvolutionPhrase)
        return;
    lastEvolutionPhrase = phrase;
    std::int64_t phase = phrase % evolutionEvery;
    if (phase < 0)
        phase += evolutionEvery;
    if (phase != 0)
        return;
    ++evolutionStep;
    publishSceneLocked();
}
