#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace core = groovemorph::core;
namespace scene = groovemorph::core::scene;

namespace
{
constexpr auto stateType = "GrooveMorphState";
constexpr auto sceneTreeType = "SceneBank";
constexpr auto sceneDataProperty = "data";
constexpr int meterDenominators[] = {1, 2, 4, 8, 16};
constexpr int maxProcessedChannels = 2;

float loadParameter(const juce::AudioProcessorValueTreeState& state, const char* id, float fallback)
{
    const auto* value = state.getRawParameterValue(id);
    return value != nullptr ? value->load(std::memory_order_relaxed) : fallback;
}

int loadIndex(const juce::AudioProcessorValueTreeState& state, const char* id, int maxIndex)
{
    const auto value = loadParameter(state, id, 0.0f);
    if (!std::isfinite(value))
        return 0;
    return juce::jlimit(0, maxIndex, static_cast<int>(std::lround(value)));
}
} // namespace

GrooveMorphAudioProcessor::GrooveMorphAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, stateType, createParameterLayout())
{
    bypassParameter = dynamic_cast<juce::AudioParameterBool*>(parameters.getParameter(bypassId));
    lastSeenVariationParam = loadIndex(parameters, activeVariationId, static_cast<int>(core::variationCount) - 1);
    const std::lock_guard<std::mutex> lock(modelMutex);
    publishSceneLocked();
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
    dryBuffer.setSize(maxProcessedChannels, maxBlock, false, true, false);
    freeRunSample = 0;
    bypassStep = static_cast<float>(1.0 / juce::jmax(1.0, bypassFadeSeconds * currentSampleRate));
    const bool bypassed = bypassParameter != nullptr && bypassParameter->get();
    bypassGain = bypassed ? 1.0f : 0.0f;
    // Transient state only; saved parameters and the active variation are kept. A variation
    // parameter change made before prepare is still applied by the first block.
    scheduler.cancel();
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

core::TransitionMode GrooveMorphAudioProcessor::currentTransitionMode() const noexcept
{
    return static_cast<core::TransitionMode>(loadIndex(parameters, transitionModeId, 4));
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
    if (data == nullptr || numBytes < 3)
        return;
    const int status = data[0] & 0xf0;
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

void GrooveMorphAudioProcessor::renderSegment(juce::AudioBuffer<float>& buffer, int numChannels,
                                              int start, int length,
                                              const std::optional<core::ResolvedTiming>& blockTiming,
                                              std::size_t variation, float wetAmount,
                                              float bypassTarget) noexcept
{
    const auto& settings = audioScene.snapshot.variations[std::min(variation, core::variationCount - 1)];
    std::array<float*, maxProcessedChannels> channels{};

    for (int done = 0; done < length;)
    {
        const int n = std::min(maxBlock, length - done);
        const int offset = start + done;
        for (int c = 0; c < numChannels; ++c)
            channels[static_cast<std::size_t>(c)] = buffer.getWritePointer(c, offset);

        const bool needDry = bypassGain > 0.0f || bypassTarget > 0.0f;
        if (needDry)
            for (int c = 0; c < numChannels; ++c)
                dryBuffer.copyFrom(c, 0, buffer, c, offset, n);

        core::RhythmTiming rhythm;
        if (blockTiming)
        {
            auto t = *blockTiming;
            t.currentSample += offset;
            rhythm.samplesPerBeat = t.samplesPerBeat();
            rhythm.beatAtBlockStart = t.positionInBeats();
            rhythm.isPlaying = t.isPlaying;
        }
        engine.process(channels.data(), numChannels, n, settings, wetAmount, rhythm);

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
    }
    const int variationParam = loadIndex(parameters, activeVariationId, static_cast<int>(core::variationCount) - 1);
    if (uiResyncParam.exchange(false, std::memory_order_acq_rel))
        lastSeenVariationParam = variationParam;
    const auto mode = currentTransitionMode();
    if (uiCancel.exchange(false, std::memory_order_acq_rel))
        scheduler.cancel();
    if (variationParam != lastSeenVariationParam)
    {
        lastSeenVariationParam = variationParam;
        scheduler.request(static_cast<std::size_t>(variationParam), mode);
    }
    if (const int request = uiRequest.exchange(-1, std::memory_order_acq_rel); request >= 0)
        scheduler.request(static_cast<std::size_t>(request), mode);
    if (uiTrigger.exchange(false, std::memory_order_acq_rel))
        scheduler.trigger();

    // Timing at the block start.
    const auto denominatorIndex = loadIndex(parameters, manualMeterDenominatorId, 4);
    const core::PhraseGrid manualGrid{
        static_cast<double>(loadParameter(parameters, manualTempoId, 120.0f)),
        static_cast<int>(loadParameter(parameters, manualMeterNumeratorId, 4.0f)),
        meterDenominators[denominatorIndex],
        static_cast<int>(loadParameter(parameters, phraseBarsId, 4.0f)),
        0};
    const auto timing = core::TransportResolver::resolve(
        readHostTransport(), manualGrid, freeRunSample, currentSampleRate);
    timingValid.store(timing.has_value(), std::memory_order_relaxed);
    if (timing)
    {
        timingSource.store(static_cast<int>(timing->source), std::memory_order_relaxed);
        missingTimingFields.store(timing->missingFields, std::memory_order_relaxed);
        transportPlaying.store(timing->isPlaying, std::memory_order_relaxed);
        barInPhrase.store(timing->barInPhrase(), std::memory_order_relaxed);
        beatInBar.store(timing->beatInBar(), std::memory_order_relaxed);
    }

    const float intensity = juce::jlimit(0.0f, 1.0f, loadParameter(parameters, masterIntensityId, 0.0f));
    const float dryWet = juce::jlimit(0.0f, 1.0f, loadParameter(parameters, dryWetId, 1.0f));
    const float wetAmount = intensity * dryWet;
    const bool bypassed = forceBypass || (bypassParameter != nullptr && bypassParameter->get());
    const float bypassTarget = bypassed ? 1.0f : 0.0f;

    const auto playing = [this]() noexcept {
        return audioScene.auditioning ? audioScene.snapshot.activeIndex : scheduler.activeVariation();
    };

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
            const auto switchEvent = scheduler.process(segmentTiming, length);
            if (switchEvent.switched)
            {
                const int offset = static_cast<int>(
                    std::clamp<std::int64_t>(switchEvent.sampleOffset, 0, length - 1));
                if (offset > 0)
                    renderSegment(buffer, numChannels, position, offset, timing, before, wetAmount, bypassTarget);
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

    freeRunSample += numSamples;

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

void GrooveMorphAudioProcessor::publishSceneLocked()
{
    PublishedScene next;
    next.auditioning = audition.isActive();
    next.snapshot = next.auditioning ? audition.audibleSnapshot(undoStack.current())
                                     : undoStack.current().snapshot();
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
    auto bank = undoStack.current();
    bank.setVariationSettings(index, settings);
    commitBankLocked(bank);
}

void GrooveMorphAudioProcessor::setOperationSettings(std::size_t index, core::OperationType op,
                                                     const core::OperationSettings& settings)
{
    if (index >= core::variationCount || op >= core::OperationType::Count)
        return;
    const std::lock_guard<std::mutex> lock(modelMutex);
    auto bank = undoStack.current();
    auto variation = bank.variation(index).settings;
    variation[op] = settings;
    bank.setVariationSettings(index, variation);
    commitBankLocked(bank);
}

void GrooveMorphAudioProcessor::setVariationName(std::size_t index, std::string_view name)
{
    if (index >= core::variationCount)
        return;
    const std::lock_guard<std::mutex> lock(modelMutex);
    auto bank = undoStack.current();
    bank.setVariationName(index, name);
    commitBankLocked(bank);
}

void GrooveMorphAudioProcessor::setMidiNoteMap(const scene::MidiNoteMap& map)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    auto bank = undoStack.current();
    bank.setMidiNoteMap(map);
    commitBankLocked(bank);
}

void GrooveMorphAudioProcessor::setSceneSeed(std::uint64_t seed)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    auto bank = undoStack.current();
    bank.setSeed(seed);
    commitBankLocked(bank);
}

bool GrooveMorphAudioProcessor::undoSceneEdit()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (!undoStack.undo())
        return false;
    publishSceneLocked();
    return true;
}

bool GrooveMorphAudioProcessor::redoSceneEdit()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (!undoStack.redo())
        return false;
    publishSceneLocked();
    return true;
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
    state.removeChild(state.getChildWithName(sceneTreeType), nullptr);
    state.appendChild(sceneTree, nullptr);
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
    const auto sceneTree = restoredState.getChildWithName(sceneTreeType);
    if (sceneTree.isValid())
    {
        sceneText = sceneTree.getProperty(sceneDataProperty).toString().toStdString();
        restoredState.removeChild(sceneTree, nullptr);
    }
    parameters.replaceState(restoredState);

    // Missing or malformed scene data yields defaults plus a recovery report.
    auto result = scene::SceneSerializer::deserialize(sceneText);
    const auto active = result.bank.activeIndex();
    {
        const std::lock_guard<std::mutex> lock(modelMutex);
        audition.revert();
        undoStack.reset(result.bank);
        lastLoadReport = std::move(result.report);
        publishSceneLocked();
    }
    activeVariation.store(active, std::memory_order_relaxed);
    queuedVariation.store(-1, std::memory_order_relaxed);
    samplesUntilSwitch.store(-1, std::memory_order_relaxed);
    uiRequest.store(-1, std::memory_order_relaxed);
    uiTrigger.store(false, std::memory_order_relaxed);
    uiResyncParam.store(true, std::memory_order_release);
    uiReset.store(static_cast<int>(active), std::memory_order_release);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrooveMorphAudioProcessor();
}
