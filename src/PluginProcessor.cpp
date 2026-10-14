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
    grid.beatsPerBar = juce::jlimit(1, 16, grid.beatsPerBar);
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

core::VariationSettings GrooveMorphAudioProcessor::effectiveSettings(std::size_t variation) const noexcept
{
    const auto index = std::min(variation, core::variationCount - 1);
    const auto& base = audioScene.snapshot.variations[index];
    if (morphTargetIndex < 0 || morphWeight <= 0.0f
        || static_cast<std::size_t>(morphTargetIndex) == index)
        return base;
    return core::control::morph(base, audioScene.snapshot.variations[static_cast<std::size_t>(morphTargetIndex)],
                                morphWeight);
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
            channels[static_cast<std::size_t>(c)] = buffer.getWritePointer(c, offset);

        core::RhythmTiming rhythm;
        core::control::GridContext gridContext;
        if (blockTiming)
        {
            auto t = *blockTiming;
            t.currentSample += offset;
            if (t.ppqPosition)
                *t.ppqPosition += static_cast<double>(offset)
                    * t.grid.tempoBpm / (60.0 * t.sampleRate);
            rhythm.samplesPerBeat = t.samplesPerBeat();
            rhythm.beatAtBlockStart = t.positionInBeats();
            rhythm.isPlaying = t.isPlaying
                && (t.mode != core::TimingMode::HostPpq || t.ppqPosition.has_value());
            gridContext.beatsPerBar = t.grid.meterNumerator;
            gridContext.phraseBars = t.grid.phraseBars;
        }

        if (rhythm.isPlaying && rhythm.samplesPerBeat > 0.0 && demoEnabled.load(std::memory_order_relaxed))
            core::DemoSource::render(channels.data(), numChannels, n, rhythm.beatAtBlockStart,
                                     rhythm.samplesPerBeat, currentSampleRate);

        // C01/C03/C06/F03: operation targets for this musical position. The slice ends at the
        // next placement boundary (nearest sample, at least one sample) so event edges are
        // sample-accurate regardless of host block size or MIDI splits.
        const core::VariationSettings* render = &settings;
        core::VariationSettings gated;
        if (placementActive && rhythm.isPlaying && rhythm.samplesPerBeat > 0.0
            && std::isfinite(rhythm.beatAtBlockStart))
        {
            const auto targets = core::control::evaluate(settings.control, gridContext,
                                                         rhythm.beatAtBlockStart, enabledMask);
            if (targets.gated)
            {
                gated = settings;
                for (std::size_t i = 0; i < core::operationCount; ++i)
                {
                    if (!targets.active[i])
                        gated.operations[i].enabled = false;
                    else
                        gated.operations[i].depth *= targets.depthScale[i];
                }
                render = &gated;
                const double samples = targets.ticksUntilChange / core::control::ticksPerBeat
                                       * rhythm.samplesPerBeat;
                if (std::isfinite(samples) && samples < static_cast<double>(n))
                    n = std::max(1, static_cast<int>(std::llround(samples)));
            }
        }

        const bool needDry = bypassGain > 0.0f || bypassTarget > 0.0f;
        if (needDry)
            for (int c = 0; c < numChannels; ++c)
                dryBuffer.copyFrom(c, 0, buffer, c, offset, n);

        engine.process(channels.data(), numChannels, n, *render, wetAmount, rhythm);

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
    {
        const int morphIdx = loadIndex(cached.morphTarget, 4) - 1;
        morphTargetIndex = morphIdx;
        morphWeight = juce::jlimit(0.0f, 1.0f, loadParameter(cached.morphAmount, 0.0f));
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
    const auto timing = core::TransportResolver::resolve(
        hostTransport, manualGrid, freeRunSample, currentSampleRate, timingModeSelection);

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
        autoReturn.disarm(); // a seek/loop cancels a transient automatic return
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
        beatInBar.store(timing->beatInBar(), std::memory_order_relaxed);
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
    const int autoReturnBars = loadIndex(cached.autoReturnBars, 32);
    if (autoReturnBars == 0)
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

    autoReturnTargetStatus.store(autoReturn.armed() ? static_cast<int>(*autoReturn.target()) : -1,
                                 std::memory_order_relaxed);
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

bool GrooveMorphAudioProcessor::copyVariationSettings(std::size_t source, std::size_t destination)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive())
        return false;
    auto bank = undoStack.current();
    if (!bank.copyVariationSettings(source, destination))
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
    if (!bank.duplicateVariation(source, destination))
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
    bank.resetVariation(index);
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
    // Pre-selector projects always used Automatic. Restore that behavior even
    // when an older preset is loaded into an instance currently using Sample Clock.
    if (!restoredState.getChildWithProperty("id", timingModeId).isValid())
    {
        juce::ValueTree clockState{"PARAM"};
        clockState.setProperty("id", timingModeId, nullptr);
        clockState.setProperty("value", 0.0f, nullptr);
        restoredState.appendChild(clockState, nullptr);
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
