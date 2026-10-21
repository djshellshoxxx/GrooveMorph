#include "ui/visual/VisualWindow.h"

#include "PluginProcessor.h"
#include "core/control/VariationBlend.h"
#include "core/dsp/RhythmEngine.h"
#include "core/trace/TraceRing.h"
#include "core/viz/Ledger.h"
#include "core/viz/PlanBuilder.h"
#include "core/viz/StageModel.h"
#include "core/control/PlacementEvaluator.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::ui::visual
{
namespace viz = groovemorph::core::viz;
namespace trace = groovemorph::core::trace;

namespace
{
constexpr std::size_t maxFrames = 4096; // about 11 s at 48 kHz: covers the view, its lead and the 4 s history
constexpr double viewLeadBeats = 2.0;
constexpr double inkFrontPosition = 0.62; // fraction of the heard row to the left of "now"
} // namespace

VisualContent::VisualContent(GrooveMorphAudioProcessor& p, bool persistPreferences) : processor(p), frameBuffer(maxFrames)
{
    setLookAndFeel(&lookAndFeel);
    setTitle("GrooveMorph visual feedback");
    setWantsKeyboardFocus(true);
    if (persistPreferences)
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "GrooveMorph";
        options.filenameSuffix = ".settings";
        options.folderName = "GrooveMorph";
        options.osxLibrarySubFolder = "Application Support";
        options.commonToAllUsers = false;
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        preferences = std::make_unique<juce::PropertiesFile>(options);
        reduceMotionOn = preferences->getBoolValue("visual.reduceMotion", false);
        viewSpan = juce::jlimit(1.0, 8.0, preferences->getDoubleValue("visual.spanBeats", 4.0));
    }
    copyButton.setTooltip("Copies every number in the Change Ledger as plain text (shortcut: C), for bug reports or notes.");
    copyButton.onClick = [this] { copyLedgerToClipboard(); };
    addAndMakeVisible(copyButton);
    reduceMotionButton.setTooltip("Refreshes the picture 10 times a second instead of 30. Nothing is animated beyond data updates.");
    reduceMotionButton.setToggleState(reduceMotionOn, juce::dontSendNotification);
    reduceMotionButton.onClick = [this] { setReduceMotion(reduceMotionButton.getToggleState()); };
    addAndMakeVisible(reduceMotionButton);
    spanLabel.setText("Span", juce::dontSendNotification);
    spanLabel.setColour(juce::Label::textColourId, palette::dim);
    spanLabel.setFont(theme::font(theme::fontLabel));
    spanLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(spanLabel);
    spanBox.addItem("2 beats", 2);
    spanBox.addItem("4 beats", 4);
    spanBox.addItem("8 beats", 8);
    spanBox.setSelectedId(static_cast<int>(std::lround(viewSpan)) == 2 ? 2 : static_cast<int>(std::lround(viewSpan)) == 8 ? 8 : 4, juce::dontSendNotification);
    spanBox.setTooltip("How many beats the Warp Weave shows from left to right.");
    spanBox.onChange = [this] { setSpanBeats(static_cast<double>(spanBox.getSelectedId())); };
    addAndMakeVisible(spanBox);
    addAndMakeVisible(weaveView);
    addAndMakeVisible(ledgerView);
    addAndMakeVisible(ringsView);
    addAndMakeVisible(stationsView);
    addAndMakeVisible(orbitView);
}

VisualContent::~VisualContent()
{
    setActive(false);
    setLookAndFeel(nullptr);
}

void VisualContent::setActive(bool on)
{
    if (active == on)
        return;
    active = on;
    processor.getTraceRing().setActive(on);
    if (on)
    {
        startTimerHz(refreshRate());
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

void VisualContent::copyLedgerToClipboard()
{
    juce::SystemClipboard::copyTextToClipboard(ledgerText());
}

void VisualContent::setReduceMotion(bool on)
{
    reduceMotionOn = on;
    reduceMotionButton.setToggleState(on, juce::dontSendNotification);
    if (active)
        startTimerHz(refreshRate());
    savePreferences();
}

void VisualContent::setSpanBeats(double beats)
{
    viewSpan = juce::jlimit(1.0, 8.0, beats);
    savePreferences();
}

void VisualContent::savePreferences()
{
    if (preferences == nullptr)
        return;
    preferences->setValue("visual.reduceMotion", reduceMotionOn);
    preferences->setValue("visual.spanBeats", viewSpan);
    preferences->saveIfNeeded();
}

bool VisualContent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('c') || key == juce::KeyPress('C'))
    {
        copyLedgerToClipboard();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && onEscape)
    {
        onEscape();
        return true;
    }
    return false;
}

void VisualContent::resized()
{
    auto area = getLocalBounds().reduced(14);
    {
        auto bar = area.removeFromTop(28);
        copyButton.setBounds(bar.removeFromLeft(110));
        bar.removeFromLeft(10);
        reduceMotionButton.setBounds(bar.removeFromLeft(130));
        spanBox.setBounds(bar.removeFromRight(100));
        spanLabel.setBounds(bar.removeFromRight(60));
        area.removeFromTop(10);
    }
    const int ledgerWidth = juce::jlimit(250, 300, area.getWidth() / 4);
    ledgerView.setBounds(area.removeFromRight(ledgerWidth));
    area.removeFromRight(12);
    const int bottomHeight = juce::jlimit(170, 230, area.getHeight() / 3);
    auto bottom = area.removeFromBottom(bottomHeight);
    area.removeFromBottom(12);
    weaveView.setBounds(area);
    // Bottom row: [rings | stations | orbit]. The rings need a roughly square space; below about 900 px of
    // row width they are hidden rather than squeezing the stations.
    const bool showRings = bottom.getWidth() >= 900;
    orbitView.setBounds(bottom.removeFromRight(juce::jlimit(230, 280, bottom.getWidth() / 4)));
    bottom.removeFromRight(12);
    ringsView.setVisible(showRings);
    if (showRings)
    {
        ringsView.setBounds(bottom.removeFromLeft(juce::jlimit(200, 240, bottom.getHeight() + 30)));
        bottom.removeFromLeft(12);
    }
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
    params.spanBeats = viewSpan;
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
                                               params.viewStartBeat + params.spanBeats + 0.05, segments.data(), segments.size(),
                                               processor.getPhaseShiftBeats()));
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

    // 5. Phrase rings: laps from measured frames, placement wedge from evaluate(), planned rest of the lap.
    {
        const double phraseBeats = static_cast<double>(grid.beatsPerBar) * static_cast<double>(grid.phraseBars);
        if (static_cast<int>(phraseBeats) != lastPhraseBeats)
        {
            lapRecorder.reset(); // a different phrase length makes earlier laps incomparable
            lastPhraseBeats = static_cast<int>(phraseBeats);
        }
        lapRecorder.feed(window.frames, window.count, phraseBeats);
        RingsView::Data rings;
        rings.phraseBars = grid.phraseBars;
        rings.timed = timed;
        rings.lapCount = std::min<std::size_t>(rings.laps.size(), lapRecorder.lapCount());
        for (std::size_t i = 0; i < rings.lapCount; ++i)
            if (const auto* lap = lapRecorder.lapAt(i))
                rings.laps[i] = *lap;
        if (rings.lapCount >= 2)
        {
            auto d = viz::lapDiff(rings.laps[0], rings.laps[1]);
            for (std::size_t c = 0; c < viz::lapCells; ++c) // only cells that have data in both laps
                if (rings.laps[0].cells[c].frames == 0 || rings.laps[1].cells[c].frames == 0)
                    d.mask[c] = 0;
            rings.diffNewest = d;
        }
        rings.planned.fill(255);
        rings.allowed.fill(true);
        rings.placementActive = groovemorph::core::control::isActive(scene.control);
        if (timed)
        {
            const double nowLap = std::floor(params.nowBeat / phraseBeats);
            rings.lapIndex = static_cast<std::int64_t>(nowLap);
            rings.nowFraction = (params.nowBeat - nowLap * phraseBeats) / phraseBeats;
            std::uint32_t enabledMask = 0;
            for (std::size_t i = 0; i < groovemorph::core::operationCount; ++i)
                if (scene.operations[i].enabled)
                    enabledMask |= 1u << i;
            for (std::size_t c = 0; c < viz::lapCells; ++c)
            {
                const double beat = (nowLap + (static_cast<double>(c) + 0.5) / static_cast<double>(viz::lapCells)) * phraseBeats;
                if (rings.placementActive)
                {
                    const auto t = groovemorph::core::control::evaluate(scene.control, grid, beat, enabledMask,
                                                                         beat - processor.getPhaseShiftBeats());
                    bool any = !t.gated;
                    for (std::size_t i = 0; i < groovemorph::core::operationCount; ++i)
                        any = any || (t.gated && t.active[i] && scene.operations[i].enabled);
                    rings.allowed[c] = any;
                }
            }

            // Pencil arcs for the rest of this lap, from the committed scene (the plan starts a little before
            // now because stages read into the past). Cells the plan cannot cover stay empty.
            const double lapEnd = (nowLap + 1.0) * phraseBeats;
            std::vector<viz::PlanSegment> lapSegments(256);
            lapSegments.resize(viz::buildPlanSegments(scene, grid, params.nowBeat - 4.0, lapEnd + 0.01, lapSegments.data(), lapSegments.size(),
                                                      processor.getPhaseShiftBeats()));
            const auto* intensityParam = processor.parameters.getRawParameterValue("masterIntensity");
            const auto* dryWetParam = processor.parameters.getRawParameterValue("dryWet");
            viz::PlanContext lapCtx;
            lapCtx.segments = lapSegments.data();
            lapCtx.segmentCount = lapSegments.size();
            lapCtx.intensity = juce::jlimit(0.0f, 1.0f, (intensityParam != nullptr ? intensityParam->load() : 1.0f)
                                                             * (dryWetParam != nullptr ? dryWetParam->load() : 1.0f));
            lapCtx.samplesPerBeat = static_cast<double>(last->samplesPerBeat);
            lapCtx.historySamples = static_cast<std::size_t>(std::ceil(groovemorph::core::RhythmEngine::maxHistorySeconds * status.sampleRate));
            const std::size_t fromCell = static_cast<std::size_t>(rings.nowFraction * static_cast<double>(viz::lapCells));
            for (std::size_t c = fromCell; c < viz::lapCells; ++c)
            {
                const double beat = (nowLap + (static_cast<double>(c) + 0.5) / static_cast<double>(viz::lapCells)) * phraseBeats;
                const auto planned = viz::planThread(lapCtx, beat);
                if (!planned.valid)
                    continue;
                viz::Effect e = viz::Effect::Live;
                if (planned.wetMask & (1u << static_cast<unsigned>(groovemorph::core::OperationType::TapeStop))) e = viz::Effect::TapeStop;
                else if (planned.wetMask & (1u << static_cast<unsigned>(groovemorph::core::OperationType::HalfTime))) e = viz::Effect::HalfTime;
                else if (planned.wetMask & (1u << static_cast<unsigned>(groovemorph::core::OperationType::ReverseSlice))) e = viz::Effect::Reverse;
                else if (planned.wetMask & (1u << static_cast<unsigned>(groovemorph::core::OperationType::Repeat))) e = viz::Effect::Repeat;
                else if (planned.gateFactor < 0.25f) e = viz::Effect::Gate;
                rings.planned[c] = static_cast<std::uint8_t>(e);
            }
        }
        ringsView.setData(std::move(rings));
    }

    // 6. Chain stations and scene orbit.
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
    setLookAndFeel(&lookAndFeel);
    // This is a separate top-level window, so the editor's tooltip window never sees its components.
    tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 700);
    contentComponent = new VisualContent(processor, true);
    contentComponent->onEscape = [this] { closeButtonPressed(); };
    setContentOwned(contentComponent, false);
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(900, 560, 2600, 1600); // the smallest size the views are laid out and tested for
    centreWithSize(1280, 720);
}

VisualWindow::~VisualWindow()
{
    contentComponent->setActive(false);
    tooltipWindow.reset();
    setLookAndFeel(nullptr);
}

void VisualWindow::open()
{
    setVisible(true);
    toFront(false);
    contentComponent->setActive(true);
    contentComponent->grabKeyboardFocus();
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
