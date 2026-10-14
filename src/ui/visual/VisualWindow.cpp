#include "ui/visual/VisualWindow.h"

#include "PluginProcessor.h"
#include "core/control/VariationBlend.h"
#include "core/dsp/RhythmEngine.h"
#include "core/trace/TraceRing.h"
#include "core/viz/Ledger.h"
#include "core/viz/PlanBuilder.h"
#include "core/viz/StageModel.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::ui::visual
{
namespace viz = groovemorph::core::viz;
namespace trace = groovemorph::core::trace;

namespace
{
constexpr std::size_t maxFrames = 4096; // about 11 s at 48 kHz: covers the view, its lead and the 4 s history
constexpr double viewSpanBeats = 4.0;
constexpr double viewLeadBeats = 2.0;
constexpr double inkFrontPosition = 0.62; // fraction of the heard row to the left of "now"
} // namespace

VisualContent::VisualContent(GrooveMorphAudioProcessor& p) : processor(p), frameBuffer(maxFrames)
{
    setTitle("GrooveMorph visual feedback");
    addAndMakeVisible(weaveView);
    addAndMakeVisible(ledgerView);
    addAndMakeVisible(stationsView);
    addAndMakeVisible(orbitView);
}

VisualContent::~VisualContent()
{
    setActive(false);
}

void VisualContent::setActive(bool on)
{
    if (active == on)
        return;
    active = on;
    processor.getTraceRing().setActive(on);
    if (on)
    {
        startTimerHz(30);
        refresh();
    }
    else
    {
        stopTimer();
    }
}

void VisualContent::paint(juce::Graphics& g)
{
    g.fillAll(palette::slate);
}

void VisualContent::resized()
{
    auto area = getLocalBounds().reduced(14);
    const int ledgerWidth = juce::jlimit(260, 340, area.getWidth() / 4);
    ledgerView.setBounds(area.removeFromRight(ledgerWidth));
    area.removeFromRight(12);
    const int bottomHeight = juce::jlimit(170, 230, area.getHeight() / 3);
    auto bottom = area.removeFromBottom(bottomHeight);
    area.removeFromBottom(12);
    weaveView.setBounds(area);
    orbitView.setBounds(bottom.removeFromRight(juce::jlimit(240, 320, bottom.getWidth() / 3)));
    bottom.removeFromRight(12);
    stationsView.setBounds(bottom);
}

void VisualContent::refresh()
{
    // 1. Frames: newest epoch only.
    const std::size_t copied = processor.getTraceRing().copyLatest(frameBuffer.data(), frameBuffer.size());
    const std::size_t first = trace::latestEpochStart(frameBuffer.data(), copied);
    const viz::FrameWindow window{frameBuffer.data() + first, copied - first};
    const trace::TraceFrame* last = window.count > 0 ? &window.frames[window.count - 1] : nullptr;
    const bool timed = last != nullptr && (last->flags & trace::TimingOk) != 0 && last->samplesPerBeat > 0.0f;

    // 2. Status from the processor's relaxed atomics.
    const auto grid = processor.getGridContext();
    viz::StatusSnapshot status;
    status.timingSource = processor.getTimingSource();
    status.timingValid = processor.hasValidTiming();
    status.missingFields = processor.getMissingTimingFields();
    status.transportPlaying = processor.isTransportPlaying();
    status.barInPhrase = processor.getBarInPhrase();
    status.beatInBar = processor.getBeatInBar();
    status.meterNumerator = grid.beatsPerBar;
    status.phraseBars = grid.phraseBars;
    status.activeScene = static_cast<int>(processor.getActiveVariation());
    status.queuedScene = processor.getQueuedVariation();
    status.samplesUntilSwitch = processor.getSamplesUntilSwitch();
    status.previewing = processor.isAuditioning();
    status.sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    status.historySeconds = groovemorph::core::RhythmEngine::maxHistorySeconds;
    status.tailSeconds = processor.getTailLengthSeconds();

    // 3. Ledger text.
    viz::Ledger ledger = viz::buildLedger(window, status);
    const auto banner = ledger.banner;
    const auto bannerText = juce::String(ledger.bannerText);
    ledgerView.setLedger(std::move(ledger));

    // 4. Weave threads: heard from measurements, planned from the committed scene.
    WeaveView::Data data;
    data.beatsPerBar = grid.beatsPerBar;
    data.phraseBars = grid.phraseBars;
    data.banner = banner;
    data.bannerText = bannerText;
    const int threads = juce::jlimit(48, 256, weaveView.getWidth() / 5);
    viz::WeaveParams params;
    params.spanBeats = viewSpanBeats;
    params.leadBeats = viewLeadBeats;
    params.threads = threads;
    if (timed)
    {
        params.nowBeat = last->beat;
        params.viewStartBeat = params.nowBeat - inkFrontPosition * params.spanBeats;
    }
    data.params = params;

    // Committed scene (bank copied at about 4 Hz) with morph applied, as the engine will render it.
    const auto nowMs = juce::Time::getMillisecondCounter();
    if (nowMs - bankFetchedAt > 250u)
    {
        cachedBank = processor.getSceneBank();
        bankFetchedAt = nowMs;
    }
    const std::size_t activeIndex = std::min<std::size_t>(static_cast<std::size_t>(std::max(0, status.activeScene)), groovemorph::core::variationCount - 1);
    auto scene = cachedBank.variation(activeIndex).settings;
    const auto* morphTargetParam = processor.parameters.getRawParameterValue("morphTarget");
    const auto* morphAmountParam = processor.parameters.getRawParameterValue("morphAmount");
    const int morphIndex = morphTargetParam != nullptr ? static_cast<int>(std::lround(morphTargetParam->load())) - 1 : -1;
    const float morphAmount = morphAmountParam != nullptr ? juce::jlimit(0.0f, 1.0f, morphAmountParam->load()) : 0.0f;
    if (morphIndex >= 0 && morphIndex < 4 && static_cast<std::size_t>(morphIndex) != activeIndex && morphAmount > 0.0f)
        scene = groovemorph::core::control::morph(scene, cachedBank.variation(static_cast<std::size_t>(morphIndex)).settings, morphAmount);
    auto sceneNow = scene;
    if (timed)
    {
        std::vector<viz::Thread> heard(static_cast<std::size_t>(threads));
        heard.resize(viz::buildWeave(window, params, heard.data(), heard.size()));
        data.heard = std::move(heard);

        const double spb = static_cast<double>(last->samplesPerBeat);
        // Planned threads read back into the past (up to 4 slice-lengths), so the plan starts before the view.
        std::vector<viz::PlanSegment> segments(256);
        segments.resize(viz::buildPlanSegments(scene, grid, params.viewStartBeat - params.leadBeats - 4.0,
                                               params.viewStartBeat + params.spanBeats + 0.05, segments.data(), segments.size()));
        const auto* intensityParam = processor.parameters.getRawParameterValue("masterIntensity");
        const auto* dryWetParam = processor.parameters.getRawParameterValue("dryWet");
        viz::PlanContext ctx;
        ctx.segments = segments.data();
        ctx.segmentCount = segments.size();
        ctx.intensity = juce::jlimit(0.0f, 1.0f, (intensityParam != nullptr ? intensityParam->load() : 1.0f)
                                                     * (dryWetParam != nullptr ? dryWetParam->load() : 1.0f));
        ctx.samplesPerBeat = spb;
        ctx.historySamples = static_cast<std::size_t>(std::ceil(groovemorph::core::RhythmEngine::maxHistorySeconds * status.sampleRate));

        // Beyond a queued scene switch the plan is only a guess; evolution/gestures are not modelled.
        double tentativeFrom = params.viewStartBeat + params.spanBeats + 1.0;
        if (status.queuedScene >= 0)
            tentativeFrom = status.samplesUntilSwitch >= 0 ? params.nowBeat + static_cast<double>(status.samplesUntilSwitch) / spb
                                                           : params.nowBeat;
        std::vector<viz::Thread> planned(static_cast<std::size_t>(threads));
        planned.resize(viz::buildPlannedWeave(ctx, params, tentativeFrom, planned.data(), planned.size()));
        data.planned = std::move(planned);
        for (const auto& seg : segments)
            if (params.nowBeat >= seg.startBeat && params.nowBeat < seg.endBeat)
            {
                sceneNow = seg.settings;
                break;
            }
    }
    weaveView.setData(std::move(data));

    // 5. Chain stations and scene orbit.
    {
        std::array<std::uint8_t, groovemorph::core::operationCount> locks{};
        for (std::size_t i = 0; i < locks.size(); ++i)
            locks[i] = scene.control.permissions.operationLocks[i];
        stationsView.setStages(viz::buildStages(scene, sceneNow, locks, last),
                               status.previewing ? "PREVIEW: not the committed scene" : (timed ? "" : "effects are bypassed while timing is unavailable"));
        const double spbNow = timed ? static_cast<double>(last->samplesPerBeat) : 0.0;
        orbitView.setOrbit(viz::buildOrbit(status.activeScene, status.queuedScene, status.samplesUntilSwitch, spbNow, morphIndex,
                                           morphAmount, processor.getAutoReturnTarget(), status.previewing));
    }
}

VisualWindow::VisualWindow(GrooveMorphAudioProcessor& processor, std::function<void()> onClose)
    : juce::DocumentWindow("GrooveMorph Visual", palette::slate, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      onCloseRequested(std::move(onClose))
{
    contentComponent = new VisualContent(processor);
    setContentOwned(contentComponent, false);
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(720, 480, 2600, 1600);
    centreWithSize(1100, 680);
}

VisualWindow::~VisualWindow()
{
    contentComponent->setActive(false);
}

void VisualWindow::open()
{
    setVisible(true);
    toFront(false);
    contentComponent->setActive(true);
}

void VisualWindow::close()
{
    contentComponent->setActive(false);
    setVisible(false);
}

void VisualWindow::closeButtonPressed()
{
    close();
    if (onCloseRequested)
        onCloseRequested();
}
} // namespace groovemorph::ui::visual
