#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

// F09 follow-up services: pitch-preserving stretch to the host tempo, loop audio in the project state,
// waveform data. Everything here runs on the message thread (timer / host state calls); the audio thread
// only sees the existing LoopSource reader-flag handoff and a few relaxed atomics.

namespace core = groovemorph::core;

namespace
{
constexpr auto loopTreeType = "LoopAudio";
constexpr int loopStateVersion = 1;
} // namespace

void GrooveMorphAudioProcessor::loopContentChangedLocked()
{
    loopGeneration.fetch_add(1, std::memory_order_relaxed);
    loopBlobGeneration = ~0ull;
    stretchWorker.cancelAll();
    stretchRatioInUse = 1.0;
    stretchPublishedRatio = 1.0;
    stretchStableTicks = 0;
    loopStretchRatioStatus.store(1.0, std::memory_order_relaxed);
    loopStretchLimitedStatus.store(false, std::memory_order_relaxed);
}

void GrooveMorphAudioProcessor::publishStretchedLoop(const core::StretchWorker::Result& result)
{
    if (result.output.empty() || result.output[0].size() < 16)
        return;
    const float* channels[2] = {result.output[0].data(), result.output[result.output.size() > 1 ? 1 : 0].data()};
    if (loopSource.load(channels, 2, static_cast<std::int64_t>(result.output[0].size()), loopStoreBeats, loopStoreOrigin))
    {
        stretchPublishedRatio = result.ratio;
        loopStretchRatioStatus.store(result.ratio, std::memory_order_relaxed);
    }
    else
        stretchRatioInUse = stretchPublishedRatio; // not published: plan again on a later tick
}

void GrooveMorphAudioProcessor::updateLoopStretch()
{
    std::unique_lock<std::mutex> lock(loopServiceMutex, std::try_to_lock);
    if (!lock.owns_lock() || loopSource.capacitySamples() <= 0)
        return; // busy (import, state restore) or not prepared yet: next tick
    {
        // Print records what the loop source plays: the loop must not be swapped (new stretch copy, stretch
        // switched off, republish after a failed publish) while a print is armed or recording. Finished stretch
        // jobs wait in the worker and are published right after the print.
        const auto printState = outputCapture.state();
        if (printState == core::OutputCapture::State::Armed || printState == core::OutputCapture::State::Recording)
            return;
    }

    // A stored loop whose publish failed (finished capture, restored project) is kept and retried.
    if (loopPublishPending)
    {
        ++loopPublishRetries;
        if (republishStoredLoop())
            loopPublishPending = false;
        else
        {
            if (loopPublishRetries == 20 && loopCapture.state() == core::LoopCapture::State::Idle)
                loopCapture.setFailure(5);
            if (loopPublishRetries >= 100)
                loopPublishPending = false;
            return;
        }
    }

    const bool wanted = loadStretchFlag();
    core::StretchWorker::Result result;
    while (stretchWorker.tryTake(result))
        if (result.ok && wanted && result.id == stretchJobId)
            publishStretchedLoop(result);

    if (!wanted)
    {
        // Back to the beat-locked varispeed buffer (the exact alpha behaviour).
        if (stretchRatioInUse != 1.0 || stretchPublishedRatio != 1.0)
            republishStoredLoop();
        return;
    }

    const int samples = loopStore.getNumSamples();
    if (samples < 16 || !(loopStoreBeats > 0.0) || std::abs(loopStoreRate - currentSampleRate) > 0.5)
        return;
    const double host = hostTempoStatus.load(std::memory_order_relaxed);
    if (!(host > 0.0) || !std::isfinite(host))
        return;
    // Wait until the tempo has been steady for 200 ms so automation ramps do not queue a render per tick.
    if (std::abs(host - stretchLastTempo) > 0.001 * host)
    {
        stretchLastTempo = host;
        stretchStableTicks = 0;
        return;
    }
    if (stretchStableTicks < 4)
    {
        ++stretchStableTicks;
        return;
    }

    const double seconds = static_cast<double>(samples) / loopStoreRate;
    const double nativeBpm = loopStoreBeats * 60.0 / seconds;
    const double capacityRatio = static_cast<double>(loopSource.capacitySamples()) / static_cast<double>(samples);
    const auto plan = core::planStretch(nativeBpm, host, stretchRatioInUse, capacityRatio);
    loopStretchLimitedStatus.store(plan.limited, std::memory_order_relaxed);
    if (!plan.needsNew)
        return;

    core::StretchWorker::Job job;
    job.id = ++stretchJobId;
    job.input.resize(2);
    for (int c = 0; c < 2; ++c)
    {
        const float* source = loopStore.getReadPointer(juce::jmin(c, loopStore.getNumChannels() - 1));
        job.input[static_cast<std::size_t>(c)].assign(source, source + samples);
    }
    job.options.ratio = plan.ratio;
    job.options.sampleRate = currentSampleRate;
    job.options.circular = true;
    stretchRatioInUse = plan.ratio;
    stretchWorker.submit(std::move(job));
}

int GrooveMorphAudioProcessor::getLoopPeaks(int columns, std::vector<float>& minimums,
                                            std::vector<float>& maximums) const
{
    const std::lock_guard<std::mutex> lock(loopServiceMutex);
    const int samples = loopStore.getNumSamples();
    if (samples < 16 || columns < 1 || loopStore.getNumChannels() < 1)
    {
        minimums.clear();
        maximums.clear();
        return 0;
    }
    const auto generation = loopGeneration.load(std::memory_order_relaxed);
    if (peaksGeneration != generation || peaksColumns != columns)
    {
        const int channelCount = juce::jmin(2, loopStore.getNumChannels());
        const float* channels[2] = {loopStore.getReadPointer(0), loopStore.getReadPointer(channelCount - 1)};
        core::computePeaks(channels, channelCount, samples, columns, peaksMin, peaksMax);
        peaksGeneration = generation;
        peaksColumns = columns;
    }
    minimums = peaksMin;
    maximums = peaksMax;
    return static_cast<int>(minimums.size());
}

double GrooveMorphAudioProcessor::getLoopPlayFraction() const noexcept
{
    if (sourceMode.load(std::memory_order_relaxed) != 2 || !transportPlaying.load(std::memory_order_relaxed)
        || !loopSource.hasLoop())
        return -1.0;
    return core::loopPlayFraction(loopPlayBeatStatus.load(std::memory_order_relaxed), loopSource.publishedOrigin(),
                                  loopSource.publishedBeats());
}

void GrooveMorphAudioProcessor::appendLoopState(juce::ValueTree& state)
{
    state.removeChild(state.getChildWithName(loopTreeType), nullptr);
    const std::lock_guard<std::mutex> lock(loopServiceMutex);
    const int samples = loopStore.getNumSamples();
    if (samples < 16 || loopStoreRate <= 0.0 || !(loopStoreBeats > 0.0) || loopStore.getNumChannels() < 1)
    {
        loopPersistWarning.store(0, std::memory_order_relaxed);
        return;
    }
    const auto generation = loopGeneration.load(std::memory_order_relaxed);
    if (loopBlobGeneration != generation)
    {
        const int channelCount = juce::jmin(2, loopStore.getNumChannels());
        const float* channels[2] = {loopStore.getReadPointer(0), loopStore.getReadPointer(channelCount - 1)};
        const auto status = core::LoopBlob::encode(channels, channelCount, samples, loopStoreRate, loopStoreBeats,
                                                   loopStoreOrigin, loopBlobCache);
        loopBlobGeneration = generation;
        loopPersistWarning.store(status == core::LoopBlob::Status::Ok ? 0
                                 : status == core::LoopBlob::Status::TooLarge || status == core::LoopBlob::Status::TooLong ? 1
                                                                                                                             : 2,
                                 std::memory_order_relaxed);
        if (status != core::LoopBlob::Status::Ok)
            loopBlobCache.clear();
    }
    if (loopBlobCache.empty())
        return; // too large or not encodable: the project is saved without the loop audio
    juce::ValueTree tree(loopTreeType);
    tree.setProperty("v", loopStateVersion, nullptr);
    tree.setProperty("blob", juce::Base64::toBase64(loopBlobCache.data(), loopBlobCache.size()), nullptr);
    tree.setProperty("source", sourceMode.load(std::memory_order_relaxed), nullptr);
    state.appendChild(tree, nullptr);
}

void GrooveMorphAudioProcessor::restoreLoopState(juce::ValueTree& restoredState)
{
    const auto tree = restoredState.getChildWithName(loopTreeType);
    if (!tree.isValid())
        return; // projects saved before loop persistence: leave the session's loop as it is
    restoredState.removeChild(tree, nullptr);

    const auto fail = [this] { loopPersistWarning.store(3, std::memory_order_relaxed); };
    if (static_cast<int>(tree.getProperty("v", 0)) != loopStateVersion)
        return fail();
    const juce::String text = tree.getProperty("blob").toString();
    constexpr int maxBase64Chars = static_cast<int>(core::LoopBlob::maxEncodedBytes / 3 * 4 + 16);
    if (text.isEmpty() || text.length() > maxBase64Chars)
        return fail();
    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, text))
        return fail();
    core::LoopBlob::Audio audio;
    if (core::LoopBlob::decode(static_cast<const std::uint8_t*>(decoded.getData()), decoded.getDataSize(), audio)
        != core::LoopBlob::Status::Ok)
        return fail();

    const int samples = static_cast<int>(audio.numSamples());
    const int source = juce::jlimit(0, 2, static_cast<int>(tree.getProperty("source", 0)));
    {
        const std::lock_guard<std::mutex> lock(loopServiceMutex);
        loopStore.setSize(2, samples, false, true, false);
        loopStore.copyFrom(0, 0, audio.left.data(), samples);
        loopStore.copyFrom(1, 0, audio.right.data(), samples);
        loopStoreRate = audio.sampleRate;
        loopStoreBeats = audio.loopBeats;
        loopStoreOrigin = audio.originBeats;
        const double seconds = static_cast<double>(samples) / audio.sampleRate;
        loopSeconds.store(seconds, std::memory_order_relaxed);
        loopBeatsStatus.store(audio.loopBeats, std::memory_order_relaxed);
        loopBpmStatus.store(audio.loopBeats * 60.0 / seconds, std::memory_order_relaxed);
        loopContentChangedLocked();
        loopPersistWarning.store(0, std::memory_order_relaxed);
        // Publish now when the processor is already prepared; otherwise prepareToPlay republishes the store
        // (resampled to the device rate). The audio thread is never involved beyond the reader-flag handoff.
        loopPublishPending = false;
        if (loopSource.capacitySamples() > 0)
            loopPublishPending = !republishStoredLoop();
        loopPublishRetries = 0;
    }
    if (source == 2)
        setSourceMode(2);
}
