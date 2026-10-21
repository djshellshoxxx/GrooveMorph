#include "ui/EuclidPanel.h"

#include "PluginProcessor.h"
#include "core/control/Chance.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace gm = groovemorph::core;
namespace ctl = groovemorph::core::control;

namespace groovemorph::ui
{
namespace
{
const juce::StringArray operationNames{"Repeat / stutter", "Reverse slice", "Gate", "Half-time", "Tape stop", "Filter sweep", "Bit crush"};
static_assert(theme::effectCount == static_cast<int>(groovemorph::core::operationCount), "one theme colour per operation");
constexpr std::uint8_t gridValues[] = {1, 2, 3, 4, 6, 8, 12, 16};
constexpr int rowHeight = theme::minTarget;
constexpr int rowGap = theme::space1;
constexpr int labelWidth = 92;

juce::Rectangle<int> nextRow(juce::Rectangle<int>& area, int height = rowHeight)
{
    auto row = area.removeFromTop(height);
    area.removeFromTop(rowGap);
    return row;
}
} // namespace

void EuclidPanel::setupRow(Row& row, const juce::String& text, const juce::String& tip, double lo, double hi,
                           double step, int decimals, const juce::String& suffix)
{
    row.label.setText(text, juce::dontSendNotification);
    row.label.setFont(theme::font(theme::fontLabel));
    row.label.setJustificationType(juce::Justification::centredLeft);
    row.label.setColour(juce::Label::textColourId, theme::text);
    row.label.setTooltip(tip);
    row.slider.setSliderStyle(juce::Slider::LinearHorizontal);
    row.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 26);
    row.slider.setTitle(text);
    row.slider.setRange(lo, hi, step);
    row.slider.setNumDecimalPlacesToDisplay(decimals);
    row.slider.setTextValueSuffix(suffix);
    row.slider.setTooltip(tip);
    body.addAndMakeVisible(row.label);
    body.addAndMakeVisible(row.slider);
}

EuclidPanel::EuclidPanel(GrooveMorphAudioProcessor& p, SceneEditAccess a) : processor(p), access(std::move(a))
{
    setOpaque(true);
    body.setOpaque(true);
    body.onPaint = [this](juce::Graphics& g) { paintBody(g); };
    viewport.setViewedComponent(&body, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(10);
    viewport.setWantsKeyboardFocus(false);
    addAndMakeVisible(viewport);

    operationBox.addItemList(operationNames, 1);
    operationBox.setSelectedId(3, juce::dontSendNotification); // Gate: the most natural rhythmic target
    operationBox.setTooltip("Effect lane that the pattern fills. Also the lane used by 'Lane chance' below.");
    operationBox.onChange = [this] { body.repaint(); refresh(access.settings(), selected); };
    gridBox.addItemList({"1 per beat", "2 per beat", "3 per beat", "4 per beat", "6 per beat", "8 per beat",
                         "12 per beat", "16 per beat"}, 1);
    gridBox.setSelectedId(4, juce::dontSendNotification);
    gridBox.setTooltip("Length of one pattern step, as divisions of a meter beat.");
    gridBox.onChange = [this] { body.repaint(); };
    gateBox.addItemList({"Length 25%", "Length 50%", "Length 75%", "Length 100%"}, 1);
    gateBox.setSelectedId(2, juce::dontSendNotification);
    gateBox.setTooltip("Event length as a share of one pattern step.");
    gateBox.onChange = [this] { body.repaint(); };
    operationBox.setTitle("Euclid target effect");
    gridBox.setTitle("Euclid step grid");
    gateBox.setTitle("Euclid event length");
    body.addAndMakeVisible(operationBox);
    body.addAndMakeVisible(gridBox);
    body.addAndMakeVisible(gateBox);

    setupRow(steps, "Steps", "Length of the pattern in grid steps (1-32). The pattern repeats across the phrase.", 1, 32, 1, 0);
    setupRow(pulses, "Pulses", "Number of hits spread as evenly as possible over the steps (Euclidean rhythm).", 0, 32, 1, 0);
    setupRow(rotation, "Rotation", "Moves the whole pattern later (positive) or earlier (negative) by whole steps.", -31, 31, 1, 0);
    setupRow(accentEvery, "Accent every", "0 = off. Otherwise every Nth hit is an accent: full depth and always plays. The other hits are softer and use Density.", 0, 16, 1, 0);
    setupRow(density, "Density", "Chance that a non-accent hit plays (100 = always). With accents off it applies to every hit.", 0, 100, 1, 0, " %");
    steps.slider.setValue(8, juce::dontSendNotification);
    pulses.slider.setValue(3, juce::dontSendNotification);
    rotation.slider.setValue(0, juce::dontSendNotification);
    accentEvery.slider.setValue(0, juce::dontSendNotification);
    density.slider.setValue(100, juce::dontSendNotification);
    for (auto* r : {&steps, &pulses, &rotation, &accentEvery, &density})
        r->slider.onValueChange = [this] { syncPulseRange(); body.repaint(); };

    // Chance rows commit once per drag (one undo step), like the other Place sliders.
    setupRow(eventChance, "Event chance", "Chance that the selected event plays in each phrase cycle. 100 = always. The decision is a repeatable hash, so playback is identical every time.", 0, 100, 1, 0, " %");
    setupRow(laneChance, "Lane chance", "Chance multiplier for every event of the lane chosen above. Combines with each event's own chance.", 0, 100, 1, 0, " %");
    eventChance.slider.setValue(100, juce::dontSendNotification);
    laneChance.slider.setValue(100, juce::dontSendNotification);
    for (auto* r : {&eventChance, &laneChance})
    {
        r->slider.onDragStart = [r] { r->dragging = true; };
        const bool isEvent = r == &eventChance;
        r->slider.onDragEnd = [this, r, isEvent] {
            r->dragging = false;
            if (isEvent) commitEventChance(); else commitLaneChance();
        };
        r->slider.onValueChange = [this, r, isEvent] {
            if (!r->dragging && !refreshing)
            {
                if (isEvent) commitEventChance(); else commitLaneChance();
            }
        };
    }

    previewButton.setButtonText("Preview");
    previewButton.setTooltip("Audition the pattern without saving it. Use 'Apply preview' in the main window to keep it, or revert.");
    previewButton.onClick = [this] { doPreview(); };
    applyButton.setButtonText("Apply to lane");
    applyButton.setTooltip("Replaces the unlocked events of the chosen lane with this pattern (one undo step). Locked events, protected regions and the capacity limit are respected.");
    applyButton.onClick = [this] { doApply(); };
    clearButton.setButtonText("Clear lane");
    clearButton.setTooltip("Removes the unlocked events of the chosen lane (one undo step).");
    clearButton.onClick = [this] { doClear(); };
    body.addAndMakeVisible(previewButton);
    body.addAndMakeVisible(applyButton);
    body.addAndMakeVisible(clearButton);

    status.setFont(theme::font(theme::fontLabel));
    status.setJustificationType(juce::Justification::topLeft);
    status.setColour(juce::Label::textColourId, theme::warning);
    body.addAndMakeVisible(status);
}

std::uint8_t EuclidPanel::targetOperation() const
{
    return static_cast<std::uint8_t>(std::clamp(operationBox.getSelectedId() - 1, 0, static_cast<int>(ctl::laneCount) - 1));
}

ctl::EuclidParams EuclidPanel::params() const
{
    ctl::EuclidParams p;
    p.steps = static_cast<std::uint8_t>(steps.slider.getValue());
    p.pulses = static_cast<std::uint8_t>(pulses.slider.getValue());
    p.rotation = static_cast<std::int8_t>(rotation.slider.getValue());
    p.operation = targetOperation();
    p.subdivision = gridValues[std::clamp(gridBox.getSelectedId() - 1, 0, 7)];
    p.gatePercent = static_cast<std::uint8_t>(25 * std::clamp(gateBox.getSelectedId(), 1, 4));
    p.accentEvery = static_cast<std::uint8_t>(accentEvery.slider.getValue());
    p.density = static_cast<std::uint8_t>(density.slider.getValue());
    ctl::sanitize(p);
    return p;
}

void EuclidPanel::syncPulseRange()
{
    const double n = steps.slider.getValue();
    pulses.slider.setRange(0, n, 1);
    if (pulses.slider.getValue() > n)
        pulses.slider.setValue(n, juce::dontSendNotification);
}

void EuclidPanel::showReport(const ctl::EuclidReport& r, const juce::String& verb)
{
    if (r.status == ctl::EuclidStatus::OperationLocked)
    {
        status.setText("This lane's events are locked (Generate tab > Locks).", juce::dontSendNotification);
        return;
    }
    if (r.status == ctl::EuclidStatus::Invalid)
    {
        status.setText("Nothing to do.", juce::dontSendNotification);
        return;
    }
    juce::String text = verb + " " + juce::String(static_cast<int>(r.added)) + " events.";
    if (r.skippedRegion > 0) text << " " << juce::String(static_cast<int>(r.skippedRegion)) << " in protected regions skipped.";
    if (r.skippedOverlap > 0) text << " " << juce::String(static_cast<int>(r.skippedOverlap)) << " overlapping skipped.";
    if (r.skippedCapacity > 0) text << " Lane full: " << juce::String(static_cast<int>(r.skippedCapacity)) << " dropped.";
    if (r.keptLocked > 0) text << " " << juce::String(static_cast<int>(r.keptLocked)) << " locked kept.";
    status.setText(text, juce::dontSendNotification);
}

void EuclidPanel::doPreview()
{
    if (!access.previewCandidate)
        return;
    ctl::EuclidReport report;
    const auto candidate = ctl::buildEuclidCandidate(access.settings(), params(), processor.getGridContext().phraseTicks(),
                                                     &report, access.editingIndex());
    showReport(report, "Previewing");
    if (report.status == ctl::EuclidStatus::Ok)
        access.previewCandidate(candidate);
}

void EuclidPanel::doApply()
{
    ctl::EuclidReport report;
    const auto candidate = ctl::buildEuclidCandidate(access.settings(), params(), processor.getGridContext().phraseTicks(),
                                                     &report, access.editingIndex());
    showReport(report, "Placed");
    if (report.status == ctl::EuclidStatus::Ok)
        access.setSettings(candidate);
}

void EuclidPanel::doClear()
{
    ctl::EuclidReport report;
    const auto candidate = ctl::buildClearCandidate(access.settings(), targetOperation(),
                                                    processor.getGridContext().phraseTicks(), &report);
    if (report.status == ctl::EuclidStatus::Ok)
    {
        status.setText("Removed " + juce::String(static_cast<int>(report.removed)) + " events"
                           + (report.keptLocked > 0 ? juce::String(", ") + juce::String(static_cast<int>(report.keptLocked)) + " locked kept." : juce::String(".")),
                       juce::dontSendNotification);
        access.setSettings(candidate);
    }
    else
        showReport(report, "Removed");
}

void EuclidPanel::commitEventChance()
{
    if (refreshing || selected < 0)
        return;
    auto s = access.settings();
    const auto i = static_cast<std::size_t>(selected);
    if (i >= s.control.lane.count)
        return;
    s.control.lane.events[i].chance = static_cast<std::uint8_t>(std::clamp(eventChance.slider.getValue(), 0.0, 100.0));
    ctl::ensureChanceSeed(s.control.lane, access.editingIndex());
    access.setSettings(s);
}

void EuclidPanel::commitLaneChance()
{
    if (refreshing)
        return;
    auto s = access.settings();
    s.control.lane.chanceByOp[targetOperation()] = static_cast<std::uint8_t>(std::clamp(laneChance.slider.getValue(), 0.0, 100.0));
    ctl::ensureChanceSeed(s.control.lane, access.editingIndex());
    access.setSettings(s);
}

void EuclidPanel::refresh(const gm::VariationSettings& s, int selectedEvent)
{
    const juce::ScopedValueSetter<bool> guard(refreshing, true);
    const auto& lane = s.control.lane;
    const auto op = targetOperation();
    selected = (lane.explicitEvents && selectedEvent >= 0 && static_cast<std::size_t>(selectedEvent) < lane.count) ? selectedEvent : -1;

    const bool opLocked = (s.control.permissions.operationLocks[op] & ctl::lock::events) != 0;
    applyButton.setEnabled(!opLocked);
    clearButton.setEnabled(!opLocked);
    previewButton.setEnabled(!opLocked);

    if (!laneChance.dragging && !laneChance.slider.isMouseButtonDown() && !laneChance.slider.hasKeyboardFocus(true))
        laneChance.slider.setValue(lane.chanceByOp[op], juce::dontSendNotification);
    laneChance.slider.setEnabled(lane.explicitEvents && !opLocked);

    const bool haveSel = selected >= 0;
    selectedLocked = false;
    if (haveSel)
    {
        const auto& e = lane.events[static_cast<std::size_t>(selected)];
        selectedLocked = e.locked || (s.control.permissions.operationLocks[e.operation % ctl::laneCount] & ctl::lock::events) != 0;
        if (!eventChance.dragging && !eventChance.slider.isMouseButtonDown() && !eventChance.slider.hasKeyboardFocus(true))
            eventChance.slider.setValue(e.chance, juce::dontSendNotification);
    }
    eventChance.slider.setEnabled(haveSel && !selectedLocked);
    if (!haveSel)
        eventChance.slider.setValue(100, juce::dontSendNotification);
}

void EuclidPanel::resized()
{
    viewport.setBounds(getLocalBounds().reduced(1));
    int width = viewport.getWidth();
    int height = layoutBody(width);
    if (height > viewport.getHeight())
    {
        width -= viewport.getScrollBarThickness();
        height = layoutBody(width);
    }
    body.setSize(width, std::max(height, viewport.getHeight()));
}

// Lays the controls out for a 280 px drawer (one column) and returns the content height.
int EuclidPanel::layoutBody(int width)
{
    auto area = juce::Rectangle<int>(theme::space2, theme::space2, std::max(40, width - 2 * theme::space2), 100000);
    area.removeFromTop(20); // heading painted in paintBody
    operationBox.setBounds(nextRow(area));
    auto combos = nextRow(area);
    gridBox.setBounds(combos.removeFromLeft(combos.getWidth() / 2).reduced(0, 0).withTrimmedRight(theme::space1));
    gateBox.setBounds(combos);

    // Ring preview on top, then the five pattern rows (label left of the slider).
    const int ringSize = std::clamp(area.getWidth() - 2 * theme::space4, 96, 140);
    ringBounds = area.removeFromTop(ringSize).withSizeKeepingCentre(ringSize, ringSize);
    area.removeFromTop(theme::space2);
    for (auto* r : {&steps, &pulses, &rotation, &accentEvery, &density})
    {
        auto row = nextRow(area);
        r->label.setBounds(row.removeFromLeft(labelWidth));
        r->slider.setBounds(row);
    }

    auto buttons = nextRow(area);
    previewButton.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).withTrimmedRight(theme::space1));
    applyButton.setBounds(buttons);
    clearButton.setBounds(nextRow(area));

    for (auto* r : {&eventChance, &laneChance})
    {
        auto row = nextRow(area);
        r->label.setBounds(row.removeFromLeft(labelWidth));
        r->slider.setBounds(row);
    }
    status.setBounds(nextRow(area, 56));
    return area.getY() + theme::space2;
}

void EuclidPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::surface);
    g.setColour(theme::stroke);
    g.drawRect(getLocalBounds(), 1);
}

void EuclidPanel::paintBody(juce::Graphics& g)
{
    g.fillAll(theme::surface);
    g.setColour(theme::text);
    g.setFont(theme::font(theme::fontLabel, true));
    g.drawText("Euclid pattern and chance", theme::space2, theme::space2, std::max(40, body.getWidth() - 2 * theme::space2), 16,
               juce::Justification::centredLeft, true);
    if (ringBounds.isEmpty())
        return;

    const auto p = params();
    const auto hits = ctl::euclidSteps(p);
    const auto colour = theme::effectColour(p.operation);
    const auto bounds = ringBounds.toFloat();
    const auto centre = bounds.getCentre();
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f - 7.0f;
    if (radius < 8.0f)
        return;

    g.setColour(theme::strokeStrong);
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    const auto pointAt = [&](int step) {
        const float angle = juce::MathConstants<float>::twoPi * static_cast<float>(step) / static_cast<float>(p.steps)
                            - juce::MathConstants<float>::halfPi; // step 0 at 12 o'clock
        return juce::Point<float>(centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle));
    };

    // Polygon through the hits.
    juce::Path poly;
    bool started = false;
    for (int i = 0; i < p.steps; ++i)
        if (hits[static_cast<std::size_t>(i)])
        {
            const auto pt = pointAt(i);
            if (!started) { poly.startNewSubPath(pt); started = true; }
            else poly.lineTo(pt);
        }
    if (started)
    {
        poly.closeSubPath();
        g.setColour(colour.withAlpha(0.35f));
        g.strokePath(poly, juce::PathStrokeType(1.2f));
    }

    int pulseIndex = 0;
    for (int i = 0; i < p.steps; ++i)
    {
        const auto pt = pointAt(i);
        const bool hit = hits[static_cast<std::size_t>(i)];
        const bool accent = hit && p.accentEvery > 0 && (pulseIndex % p.accentEvery) == 0;
        if (hit)
            ++pulseIndex;
        const float r = hit ? (accent ? 5.5f : 4.0f) : 2.0f;
        if (hit)
        {
            g.setColour(colour.withAlpha(p.accentEvery > 0 && !accent ? 0.6f : 1.0f));
            g.fillEllipse(pt.x - r, pt.y - r, r * 2.0f, r * 2.0f);
        }
        else
        {
            g.setColour(theme::textDim);
            g.drawEllipse(pt.x - r, pt.y - r, r * 2.0f, r * 2.0f, 1.0f);
        }
        if (i == 0)
        {
            g.setColour(theme::playhead);
            g.drawEllipse(pt.x - r - 2.5f, pt.y - r - 2.5f, (r + 2.5f) * 2.0f, (r + 2.5f) * 2.0f, 1.0f); // step 0 marker
        }
    }
    g.setColour(theme::text);
    g.setFont(theme::font(theme::fontLabel));
    g.drawText(juce::String(p.pulses) + "/" + juce::String(p.steps), ringBounds.reduced(radius > 30.0f ? 20 : 0),
               juce::Justification::centred);
}
} // namespace groovemorph::ui
