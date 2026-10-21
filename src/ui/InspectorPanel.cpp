#include "ui/InspectorPanel.h"
#include "ui/ControlMenu.h"
#include "ui/EuclidPanel.h"
#include "ui/WaveformView.h"
#include "ui/PrintPanel.h"

#include "PluginProcessor.h"
#include "core/control/PhraseFit.h"
#include "core/control/PlacementEvaluator.h"
#include "core/control/SceneControl.h"
#include "core/scene/SceneSerializer.h"
#include "core/variation/CandidateBuilder.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <iomanip>
#include <iterator>
#include <sstream>

namespace gm = groovemorph::core;
namespace ctl = groovemorph::core::control;
namespace gvar = groovemorph::core::variation;

namespace groovemorph::ui
{
namespace
{
const juce::StringArray operationNames{"Repeat / stutter", "Reverse slice", "Gate", "Half-time", "Tape stop", "Filter sweep", "Bit crush"};
const juce::StringArray operationShort{"Rpt", "Rev", "Gate", "Half", "Stop", "Sweep", "Crush"};
const juce::Colour operationColours[] = {juce::Colour(0xff4fc3f7), juce::Colour(0xffba68c8),
                                         juce::Colour(0xff81c784), juce::Colour(0xffffb74d),
                                         juce::Colour(0xffe57373), juce::Colour(0xffdce775),
                                         juce::Colour(0xfff48fb1)};
static_assert(std::size(operationColours) == groovemorph::core::operationCount, "one lane colour per operation");
constexpr std::uint8_t snapValues[] = {1, 2, 3, 4, 6, 8, 12, 16};
constexpr int rowHeight = 26;
constexpr int rowGap = 4;

juce::Rectangle<int> nextRow(juce::Rectangle<int>& area, int height = rowHeight)
{
    auto row = area.removeFromTop(height);
    area.removeFromTop(rowGap);
    return row;
}

void setupLabel(juce::Label& label, const juce::String& text, const juce::String& tip = {})
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    if (tip.isNotEmpty())
        label.setTooltip(tip);
}

// Slider that commits once when released (or immediately for keyboard/wheel/text edits) so a
// drag produces a single undo entry, matching the main operation sliders.
struct SliderRow
{
    juce::Label label;
    MenuSlider slider;
    bool dragging = false;
    std::function<void()> commit;

    void setup(juce::Component& parent, const juce::String& text, const juce::String& tip, double lo,
               double hi, double step, int decimals)
    {
        setupLabel(label, text, tip);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
        slider.setRange(lo, hi, step);
        slider.setNumDecimalPlacesToDisplay(decimals);
        slider.setTooltip(tip);
        slider.onDragStart = [this] { dragging = true; };
        slider.onDragEnd = [this] { dragging = false; if (commit) commit(); };
        slider.onValueChange = [this] { if (!dragging && commit) commit(); };
        parent.addAndMakeVisible(label);
        parent.addAndMakeVisible(slider);
    }
    void layout(juce::Rectangle<int> row, int labelWidth = 132)
    {
        label.setBounds(row.removeFromLeft(labelWidth));
        slider.setBounds(row);
    }
    void set(double value)
    {
        if (!slider.isMouseButtonDown() && !slider.hasKeyboardFocus(true))
            slider.setValue(value, juce::dontSendNotification);
    }
};

class InspectorTab : public juce::Component
{
public:
    // Makes a control right-clickable for MIDI learn on a host parameter.
    template <class Control>
    void linkParam(Control& control, const char* parameterId)
    {
        control.info().processor = &processor;
        control.info().midiTarget = juce::String("p:") + parameterId;
    }

    InspectorTab(GrooveMorphAudioProcessor& p, SceneEditAccess a) : processor(p), access(std::move(a)) {}
    virtual void refresh(const gm::VariationSettings& settings) = 0;

protected:
    GrooveMorphAudioProcessor& processor;
    SceneEditAccess access;
    bool refreshing = false;

    // Read-modify-write of the edited scene through the editor's single edit path.
    void edit(const std::function<void(gm::VariationSettings&)>& change)
    {
        if (refreshing)
            return;
        auto settings = access.settings();
        change(settings);
        access.setSettings(settings);
    }
};

// ---------------------------------------------------------------------------------------
// Event lane canvas (A01 / C01)
// ---------------------------------------------------------------------------------------
class LaneCanvas final : public juce::Component, public juce::SettableTooltipClient
{
public:
    LaneCanvas(GrooveMorphAudioProcessor& p, SceneEditAccess a) : processor(p), access(std::move(a))
    {
        setWantsKeyboardFocus(true);
        setTooltip("Event lanes. Drag in an empty lane to create an event, drag a body to move it, "
                   "drag its edges to resize, double-click or press Delete to remove it. Arrow keys "
                   "nudge by one grid step (Shift: one beat). Events snap to the Snap grid. "
                   "Very narrow events: drag moves, Shift-drag resizes the end, Alt-drag the start. "
                   "Keyboard: with no event selected, Left/Right/Up/Down move a cursor and Enter (or Insert, N) "
                   "creates an event there; Escape deselects.");
        setTitle("Event lanes");
        setDescription("Event lanes, one row per effect. Arrow keys move a cursor, Enter creates an event.");
    }

    std::function<void()> onSelectionChanged;
    std::function<void(const juce::String&)> onMessage;

    int selected() const noexcept { return selectedIndex; }
    void setSettingsView(const gm::VariationSettings& s)
    {
        if (!dragging)
        {
            draft = s.control;
            // The selection follows the event itself (operation, onset, duration), not its array
            // slot, so undo, Reduce, scene switches and lock merges never leave it on another event.
            if (hasSelection)
            {
                selectedIndex = findSelected(draft.lane);
                hasSelection = selectedIndex >= 0;
            }
        }
        view = s;
        // U3: tell assistive technology about events that wrap past the phrase end (the badge is visual only).
        {
            const auto fit = ctl::checkPhraseFit(draft.lane, processor.getGridContext().phraseTicks(), &draft.permissions);
            const juce::String warning = fit.conflict()
                ? juce::String(fit.affected()) + " event(s) extend past the phrase end and wrap around. Limit tab, Events x2 or /2, rescales them."
                : juce::String();
            if (warning != lastWarning)
            {
                lastWarning = warning;
                setDescription("Event lanes, one row per effect. Arrow keys move a cursor, Enter creates an event."
                               + (warning.isEmpty() ? juce::String() : " " + warning));
            }
        }
        repaint();
    }
    bool deleteSelected()
    {
        if (selectedIndex < 0)
            return false;
        auto next = access.settings().control;
        if (isEventLocked(next.lane.events[static_cast<std::size_t>(selectedIndex)]))
            return false;
        ctl::removeEvent(next.lane, static_cast<std::size_t>(selectedIndex));
        selectedIndex = -1;
        hasSelection = false;
        commit(next);
        if (onSelectionChanged)
            onSelectionChanged();
        return true;
    }

    void paint(juce::Graphics& g) override
    {
        const auto grid = processor.getGridContext();
        const double P = static_cast<double>(grid.phraseTicks());
        const auto area = getLocalBounds();
        g.fillAll(juce::Colour(0xff10151c));
        const int left = labelWidth;
        const int width = std::max(1, area.getWidth() - left - 4);
        const auto& control = draft;

        // Placement window shading, shown for the first phrase that passes the cadence.
        if (control.placement.enabled)
        {
            ctl::SceneControl windowOnly;
            windowOnly.placement = control.placement;
            windowOnly.placement.downbeatProtectBeats = 0.0f;
            const double phraseBeats = P / ctl::ticksPerBeat;
            const double base = std::floor(0.0) + static_cast<double>(control.placement.cadenceOffset
                                   % std::max<std::uint8_t>(1, control.placement.every)) * phraseBeats;
            const double originBeats = static_cast<double>(control.placement.originTicks) / ctl::ticksPerBeat;
            for (int x = 0; x < width; ++x)
            {
                const double beats = base + originBeats + (x + 0.5) / width * phraseBeats;
                if (ctl::evaluate(windowOnly, grid, beats, 0u).active[0])
                {
                    g.setColour(juce::Colour(0x2200e5ff));
                    g.fillRect(left + x, laneTop - 2, 1, laneCount * laneHeight + 2);
                }
            }
        }

        // Ruler: bars, beats, snap subdivisions.
        const int beats = grid.beatsPerBar * grid.phraseBars;
        const int subdivision = std::max<int>(1, control.placement.subdivision);
        g.setFont(11.0f);
        for (int b = 0; b <= beats; ++b)
        {
            const bool bar = b % grid.beatsPerBar == 0;
            const float x = left + static_cast<float>(b) / beats * width;
            g.setColour(bar ? juce::Colour(0xff8aa0b8) : juce::Colour(0xff3d4b5c));
            g.drawVerticalLine(static_cast<int>(x), static_cast<float>(bar ? 2 : laneTop - 6),
                               static_cast<float>(laneTop + laneCount * laneHeight));
            if (bar && b < beats)
            {
                g.setColour(juce::Colours::lightgrey);
                g.drawText(juce::String(b / grid.beatsPerBar + 1), static_cast<int>(x) + 3, 0, 24, 14,
                           juce::Justification::centredLeft);
            }
            if (b < beats && width / beats / subdivision >= 4)
            {
                g.setColour(juce::Colour(0xff252f3b));
                for (int s = 1; s < subdivision; ++s)
                    g.drawVerticalLine(static_cast<int>(x + static_cast<float>(s) * width / beats / subdivision),
                                       static_cast<float>(laneTop), static_cast<float>(laneTop + laneCount * laneHeight));
            }
        }

        // Lanes.
        for (int lane = 0; lane < laneCount; ++lane)
        {
            const int y = laneTop + lane * laneHeight;
            g.setColour(lane % 2 ? juce::Colour(0x14ffffff) : juce::Colour(0x0affffff));
            g.fillRect(0, y, area.getWidth(), laneHeight);
            g.setColour(operationColours[lane]);
            g.drawText(operationShort[lane] + (isLocked(static_cast<std::uint8_t>(lane)) ? " (locked)" : ""),
                       4, y, left - 6, laneHeight, juce::Justification::centredLeft);
            if (!control.lane.explicitEvents)
            {
                const auto& op = view.operations[static_cast<std::size_t>(lane)];
                if (op.enabled)
                {
                    g.setColour(operationColours[lane].withAlpha(0.22f));
                    g.fillRect(left, y + 3, width, laneHeight - 6);
                }
            }
        }

        // Events.
        if (control.lane.explicitEvents)
        {
            for (std::size_t i = 0; i < control.lane.count; ++i)
            {
                const auto& e = control.lane.events[i];
                const auto ranges = eventRanges(e, P, width);
                for (const auto& r : ranges)
                {
                    juce::Rectangle<float> rect(static_cast<float>(left + r.first),
                                                static_cast<float>(laneTop + e.operation * laneHeight + 3),
                                                static_cast<float>(std::max(2, r.second - r.first)),
                                                static_cast<float>(laneHeight - 6));
                    g.setColour(operationColours[e.operation % gm::operationCount].withAlpha(e.enabled ? 0.35f + 0.55f * e.depth : 0.15f));
                    g.fillRoundedRectangle(rect, 3.0f);
                    const int laneChancePct = control.lane.chanceByOp[e.operation % laneCount];
                    if (e.chance < 100 || laneChancePct < 100) // N03: events that do not always play are hatched
                    {
                        juce::Graphics::ScopedSaveState hatchState(g);
                        g.reduceClipRegion(rect.toNearestInt());
                        g.setColour(juce::Colours::black.withAlpha(0.5f));
                        for (float hx = rect.getX() - rect.getHeight(); hx < rect.getRight(); hx += 5.0f)
                            g.drawLine(hx, rect.getBottom(), hx + rect.getHeight(), rect.getY(), 1.0f);
                        if (rect.getWidth() > 34.0f)
                        {
                            g.setColour(juce::Colours::white);
                            g.setFont(10.0f);
                            g.drawText(juce::String(e.chance * laneChancePct / 100) + "%", rect.toNearestInt(),
                                       juce::Justification::centred);
                        }
                    }
                    g.setColour(static_cast<int>(i) == selectedIndex ? juce::Colours::white
                                                                     : operationColours[e.operation % gm::operationCount]);
                    g.drawRoundedRectangle(rect, 3.0f, static_cast<int>(i) == selectedIndex ? 2.0f : 1.0f);
                    if (rect.getWidth() < static_cast<float>(narrowEventPixels))
                        g.fillRect(rect.getX() - 2.0f, rect.getY() - 3.0f, rect.getWidth() + 4.0f, 3.0f); // grip: drag to move
                    if (e.locked)
                    {
                        g.setColour(juce::Colours::white);
                        g.fillRect(rect.getX() + 3.0f, rect.getY() + 3.0f, 5.0f, 5.0f); // per-event lock mark
                    }
                }
            }
        }

        // Protected regions (C03): no effect sounds inside them.
        for (std::size_t i = 0; i < control.placement.regionCount && i < ctl::maxProtectedRegions; ++i)
        {
            const auto& region = control.placement.regions[i];
            for (const auto& r : regionRanges(region, P, width))
            {
                g.setColour(juce::Colour(0x40ff5252));
                g.fillRect(left + r.first, laneTop, std::max(2, r.second - r.first), laneCount * laneHeight);
            }
        }

        // Downbeat protection marks.
        if (control.placement.downbeatProtectBeats > 0.0f)
        {
            g.setColour(juce::Colour(0x55ff5252));
            for (int bar = 0; bar < grid.phraseBars; ++bar)
            {
                const float x = left + static_cast<float>(bar * grid.beatsPerBar) / beats * width;
                const float w = std::min<float>(control.placement.downbeatProtectBeats, static_cast<float>(grid.beatsPerBar))
                                / beats * width;
                g.fillRect(x, static_cast<float>(laneTop - 4), std::max(1.0f, w), 4.0f);
            }
        }

        // Playhead (position inside the phrase, shifted by the origin).
        if (processor.hasValidTiming() && processor.isTransportPlaying())
        {
            double ticks = processor.getBarInPhrase() * grid.beatsPerBar * ctl::ticksPerBeat
                           - static_cast<double>(control.placement.originTicks);
            ticks = std::fmod(std::fmod(ticks, P) + P, P);
            g.setColour(juce::Colours::yellow);
            g.drawVerticalLine(left + static_cast<int>(ticks / P * width), 0.0f,
                               static_cast<float>(laneTop + laneCount * laneHeight));
        }
        // Keyboard cursor (shown while the canvas has focus and no event is selected).
        if (hasKeyboardFocus(false) && selectedIndex < 0 && control.lane.explicitEvents)
        {
            const int cx = left + static_cast<int>(static_cast<double>(std::clamp<std::int64_t>(kbTick, 0, grid.phraseTicks())) / P * width);
            const int cy = laneTop + std::clamp(kbLane, 0, laneCount - 1) * laneHeight;
            g.setColour(juce::Colours::white);
            g.drawRect(cx - 1, cy + 1, 3, laneHeight - 2, 1);
            g.fillRect(cx - 3, cy, 7, 2);
        }
        // U3: events that no longer fit after a meter / phrase-length change wrap around; say so.
        {
            const auto fit = ctl::checkPhraseFit(control.lane, grid.phraseTicks(), &control.permissions);
            if (fit.conflict())
            {
                juce::String text;
                text << fit.affected() << (fit.affected() == 1 ? " event" : " events") << " past the phrase end (wrapped)";
                if (fit.lockedAffected > 0)
                    text << ", " << static_cast<int>(fit.lockedAffected) << " locked";
                g.setFont(11.0f);
                const int badgeWidth = juce::jmin(area.getWidth() - 8, static_cast<int>(text.length()) * 6 + 14);
                const auto badge = juce::Rectangle<int>(area.getRight() - badgeWidth - 4, 1, badgeWidth, 15);
                g.setColour(juce::Colour(0xf0b3541e));
                g.fillRoundedRectangle(badge.toFloat(), 4.0f);
                g.setColour(juce::Colours::white);
                g.drawText(text, badge, juce::Justification::centred);
            }
        }
        if (hasKeyboardFocus(false))
        {
            g.setColour(juce::Colours::white.withAlpha(0.4f));
            g.drawRect(getLocalBounds(), 1);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();
        if (event.mods.isPopupMenu())
            return; // right-click never creates or drags events
        auto control = access.settings().control;
        draft = control;
        if (!control.lane.explicitEvents)
        {
            if (onMessage)
                onMessage("Uniform mode: press 'Convert to events' (or enable event lanes) to place events.");
            return;
        }
        const auto grid = processor.getGridContext();
        const double P = static_cast<double>(grid.phraseTicks());
        const int lane = (event.y - laneTop) / laneHeight;
        if (lane < 0 || lane >= laneCount || event.x < labelWidth)
            return;
        const auto tick = tickAt(event.x, grid);
        dragLane = lane;
        kbLane = lane; // the keyboard cursor continues from the last click
        kbTick = tick;
        selectedIndex = hit(control, lane, tick, P, hitSlopTicks(P));
        hasSelection = selectedIndex >= 0;
        if (hasSelection)
            selectedEvent = control.lane.events[static_cast<std::size_t>(selectedIndex)];
        dragging = true;
        dragMoved = false;
        if (selectedIndex >= 0)
        {
            const auto& e = control.lane.events[static_cast<std::size_t>(selectedIndex)];
            if (isEventLocked(e))
            {
                dragging = false;
                if (onMessage)
                    onMessage("This lane's events are locked (Generator tab > Locks).");
                if (onSelectionChanged)
                    onSelectionChanged();
                repaint();
                return;
            }
            const double edge = std::max(6.0, 6.0 * P / std::max(1, getWidth() - labelWidth));
            const double endTick = e.onsetTicks + e.durationTicks;
            const double pixelsPerTick = static_cast<double>(std::max(1, getWidth() - labelWidth - 4)) / P;
            if (static_cast<double>(e.durationTicks) * pixelsPerTick < narrowEventPixels)
            {
                // Too narrow to have separate edges: a plain drag moves, Shift resizes the end, Alt the start.
                mode = event.mods.isShiftDown() ? Mode::ResizeEnd
                       : event.mods.isAltDown() ? Mode::ResizeStart : Mode::Move;
            }
            else if (std::abs(tick - e.onsetTicks) <= edge)
                mode = Mode::ResizeStart;
            else if (std::abs(tick - endTick) <= edge)
                mode = Mode::ResizeEnd;
            else
                mode = Mode::Move;
            grabOffset = tick - e.onsetTicks;
            original = e;
        }
        else
        {
            if (isLocked(static_cast<std::uint8_t>(lane)))
            {
                dragging = false;
                if (onMessage)
                    onMessage("This lane's events are locked (Generator tab > Locks).");
                return;
            }
            ctl::LaneEvent created;
            created.operation = static_cast<std::uint8_t>(lane);
            created.onsetTicks = static_cast<std::int32_t>(std::clamp<std::int64_t>(snap(tick, control, false), 0, grid.phraseTicks() - 1));
            created.durationTicks = static_cast<std::int32_t>(std::max<std::int64_t>(1, unit(control)));
            created.durationTicks = static_cast<std::int32_t>(std::min<std::int64_t>(created.durationTicks, grid.phraseTicks() - created.onsetTicks));
            const auto status = ctl::addEvent(draft.lane, created, grid.phraseTicks());
            if (status != ctl::EditStatus::Ok)
            {
                dragging = false;
                if (onMessage)
                    onMessage(status == ctl::EditStatus::Capacity ? "Event limit reached (" + juce::String(static_cast<int>(ctl::maxLaneEvents)) + ")."
                                                                  : "Events of one operation cannot overlap.");
                return;
            }
            selectedIndex = draft.lane.count - 1;
            hasSelection = true;
            selectedEvent = created;
            original = created;
            mode = Mode::ResizeEnd;
            dragMoved = true;
        }
        if (onSelectionChanged)
            onSelectionChanged();
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragging || selectedIndex < 0)
            return;
        const auto grid = processor.getGridContext();
        const auto pt = grid.phraseTicks();
        const auto tick = tickAt(event.x, grid);
        auto e = original;
        const auto u = unit(draft);
        const auto end = static_cast<std::int64_t>(original.onsetTicks) + original.durationTicks;
        if (mode == Mode::Move)
        {
            auto onset = snap(tick - grabOffset, draft, true);
            onset = std::clamp<std::int64_t>(onset, 0, std::max<std::int64_t>(0, pt - original.durationTicks));
            e.onsetTicks = static_cast<std::int32_t>(onset);
        }
        else if (mode == Mode::ResizeStart)
        {
            auto onset = std::clamp<std::int64_t>(snap(tick, draft, true), 0, end - 1);
            e.onsetTicks = static_cast<std::int32_t>(onset);
            e.durationTicks = static_cast<std::int32_t>(end - onset);
        }
        else
        {
            auto newEnd = std::clamp<std::int64_t>(snap(tick, draft, true), original.onsetTicks + 1, pt);
            if (newEnd - original.onsetTicks < u && u > 0 && snapEnabled)
                newEnd = std::min<std::int64_t>(pt, original.onsetTicks + u);
            e.durationTicks = static_cast<std::int32_t>(newEnd - original.onsetTicks);
        }
        auto trial = draft;
        if (ctl::replaceEvent(trial.lane, static_cast<std::size_t>(selectedIndex), e, pt) == ctl::EditStatus::Ok)
        {
            draft = trial;
            selectedEvent = e;
            dragMoved = true;
        }
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (!dragging)
            return;
        dragging = false;
        if (dragMoved)
            commit(draft);
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if (!draft.lane.explicitEvents || event.x < labelWidth)
            return;
        const auto grid = processor.getGridContext();
        const int lane = (event.y - laneTop) / laneHeight;
        if (lane < 0 || lane >= laneCount)
            return;
        auto control = access.settings().control;
        const double ringTicks = static_cast<double>(grid.phraseTicks());
        const int index = hit(control, lane, tickAt(event.x, grid), ringTicks, hitSlopTicks(ringTicks));
        if (index >= 0)
        {
            selectedIndex = index;
            deleteSelected();
        }
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (selectedIndex < 0)
            return keyboardCursorKey(key);
        const int code = key.getKeyCode();
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
            return deleteSelected();
        if (code == juce::KeyPress::escapeKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            // Back to cursor mode at the start of the event; Up/Down also step to the neighbouring lane.
            const auto grid = processor.getGridContext();
            kbLane = std::clamp(static_cast<int>(selectedEvent.operation) + (code == juce::KeyPress::upKey ? -1 : code == juce::KeyPress::downKey ? 1 : 0),
                                0, laneCount - 1);
            kbTick = std::clamp<std::int64_t>(selectedEvent.onsetTicks, 0, grid.phraseTicks());
            selectedIndex = -1;
            hasSelection = false;
            if (onSelectionChanged)
                onSelectionChanged();
            announceCursor("Event deselected.");
            repaint();
            return true;
        }
        const bool left = code == juce::KeyPress::leftKey;
        const bool right = code == juce::KeyPress::rightKey;
        if (!left && !right)
            return false;
        auto control = access.settings().control;
        const auto grid = processor.getGridContext();
        auto e = control.lane.events[static_cast<std::size_t>(selectedIndex)];
        if (isEventLocked(e))
            return true;
        const std::int64_t step = key.getModifiers().isShiftDown() ? ctl::ticksPerBeat : std::max<std::int64_t>(1, unit(control));
        auto onset = static_cast<std::int64_t>(e.onsetTicks) + (right ? step : -step);
        onset = std::clamp<std::int64_t>(onset, 0, std::max<std::int64_t>(0, grid.phraseTicks() - e.durationTicks));
        e.onsetTicks = static_cast<std::int32_t>(onset);
        if (ctl::replaceEvent(control.lane, static_cast<std::size_t>(selectedIndex), e, grid.phraseTicks()) == ctl::EditStatus::Ok)
        {
            selectedEvent = e;
            commit(control);
        }
        return true;
    }

    void select(int index)
    {
        selectedIndex = index;
        hasSelection = index >= 0;
        repaint();
    }

private:
    enum class Mode { Move, ResizeStart, ResizeEnd };
    static constexpr int laneCount = static_cast<int>(gm::operationCount);
    static constexpr int laneHeight = 24;
    static constexpr int laneTop = 18;
    static constexpr int labelWidth = 72;
    static constexpr int narrowEventPixels = 16; // narrower events move on a plain drag (U2)
    static constexpr int hitSlopPixels = 5;      // click tolerance around an event body (U2)

    GrooveMorphAudioProcessor& processor;
    SceneEditAccess access;
    int kbLane = 0;            // keyboard cursor (U1)
    std::int64_t kbTick = 0;
    juce::String lastDescription, lastWarning;
    gm::VariationSettings view;
    ctl::SceneControl draft;
    int selectedIndex = -1;
    bool hasSelection = false;
    ctl::LaneEvent selectedEvent;
    int dragLane = 0;
    bool dragging = false, dragMoved = false;
    bool snapEnabled = true;
    Mode mode = Mode::Move;
    std::int64_t grabOffset = 0;
    ctl::LaneEvent original;

    int findSelected(const ctl::EventLane& lane) const noexcept
    {
        for (std::size_t i = 0; i < lane.count; ++i)
            if (lane.events[i].operation == selectedEvent.operation && lane.events[i].onsetTicks == selectedEvent.onsetTicks
                && lane.events[i].durationTicks == selectedEvent.durationTicks)
                return static_cast<int>(i);
        return -1;
    }
    bool isEventLocked(const ctl::LaneEvent& e) const { return e.locked || isLocked(e.operation); }
    bool isLocked(std::uint8_t op) const
    {
        return op < ctl::laneCount && (access.settings().control.permissions.operationLocks[op] & ctl::lock::events) != 0;
    }
    static std::int64_t unit(const ctl::SceneControl& c) noexcept
    {
        return ctl::ticksPerBeat / std::max<int>(1, c.placement.subdivision);
    }
    std::int64_t snap(std::int64_t tick, const ctl::SceneControl& c, bool) const noexcept
    {
        const auto u = unit(c);
        return u > 0 ? static_cast<std::int64_t>(std::llround(static_cast<double>(tick) / static_cast<double>(u))) * u : tick;
    }
    std::int64_t tickAt(int x, const ctl::GridContext& grid) const noexcept
    {
        const double fraction = static_cast<double>(x - labelWidth) / std::max(1, getWidth() - labelWidth - 4);
        return static_cast<std::int64_t>(std::llround(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(grid.phraseTicks())));
    }
    // slop: extra ticks of tolerance on both sides of the event body (a few pixels), so thin events can be hit.
    static int hit(const ctl::SceneControl& c, int lane, std::int64_t tick, double P, double slop = 0.0) noexcept
    {
        for (std::size_t i = 0; i < c.lane.count; ++i)
        {
            const auto& e = c.lane.events[i];
            if (e.operation != lane)
                continue;
            const double onset = std::fmod(static_cast<double>(e.onsetTicks), P);
            const double linear = static_cast<double>(tick) - onset;
            if (linear >= -std::max(1.0, slop) && linear < static_cast<double>(e.durationTicks) + slop)
                return static_cast<int>(i);
            double d = std::fmod(linear, P); // the part of a wrapping event that continues at the phrase start
            if (d < 0.0)
                d += P;
            if (d < static_cast<double>(e.durationTicks))
                return static_cast<int>(i);
        }
        return -1;
    }
    double hitSlopTicks(double P) const noexcept
    {
        return static_cast<double>(hitSlopPixels) * P / static_cast<double>(std::max(1, getWidth() - labelWidth - 4));
    }

    // ---- U1: keyboard cursor and event creation ----
    juce::String describePosition(std::int64_t ticks, const ctl::GridContext& grid) const
    {
        const std::int64_t barTicks = static_cast<std::int64_t>(std::max(1, grid.beatsPerBar)) * ctl::ticksPerBeat;
        const auto t = std::max<std::int64_t>(0, ticks);
        return "bar " + juce::String(static_cast<int>(t / barTicks) + 1) + " beat "
               + juce::String(static_cast<int>((t % barTicks) / ctl::ticksPerBeat) + 1)
               + (t % ctl::ticksPerBeat != 0 ? " plus " + juce::String(static_cast<int>(t % ctl::ticksPerBeat)) + " ticks" : juce::String());
    }
    void announceCursor(const juce::String& what)
    {
        const auto grid = processor.getGridContext();
        juce::String text;
        const int lane = std::clamp(kbLane, 0, laneCount - 1);
        text << (what.isEmpty() ? juce::String() : what + " ") << "Cursor: " << operationName(lane) << ", " << describePosition(kbTick, grid) << ".";
        setDescription(text);
        lastDescription = text;
        if (onMessage)
            onMessage(text);
        repaint();
    }
    static juce::String operationName(int lane)
    {
        return lane >= 0 && lane < operationNames.size() ? operationNames[lane] : "Effect " + juce::String(lane + 1);
    }
    bool keyboardCursorKey(const juce::KeyPress& key)
    {
        const auto grid = processor.getGridContext();
        const auto control = access.settings().control;
        const int code = key.getKeyCode();
        const bool plain = !key.getModifiers().isCtrlDown() && !key.getModifiers().isCommandDown() && !key.getModifiers().isAltDown();
        if (!plain)
            return false;
        const std::int64_t step = key.getModifiers().isShiftDown() ? ctl::ticksPerBeat : std::max<std::int64_t>(1, unit(control));
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        {
            kbTick = std::clamp<std::int64_t>(kbTick + (code == juce::KeyPress::rightKey ? step : -step), 0, grid.phraseTicks() - 1);
            announceCursor(juce::String());
            return true;
        }
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            kbLane = std::clamp(kbLane + (code == juce::KeyPress::downKey ? 1 : -1), 0, laneCount - 1);
            announceCursor(juce::String());
            return true;
        }
        if (code == juce::KeyPress::returnKey || code == juce::KeyPress::insertKey
            || (key.getTextCharacter() == 'n' || key.getTextCharacter() == 'N'))
        {
            createAtCursor();
            return true;
        }
        return false;
    }
    void createAtCursor()
    {
        auto next = access.settings().control;
        if (!next.lane.explicitEvents)
        {
            announceCursor("Uniform mode: press 'Convert to events' (or enable event lanes) to place events.");
            return;
        }
        const int lane = std::clamp(kbLane, 0, laneCount - 1);
        if (isLocked(static_cast<std::uint8_t>(lane)))
        {
            announceCursor("This lane's events are locked (Generator tab > Locks).");
            return;
        }
        const auto grid = processor.getGridContext();
        const std::int64_t pt = grid.phraseTicks();
        ctl::LaneEvent created;
        created.operation = static_cast<std::uint8_t>(lane);
        created.onsetTicks = static_cast<std::int32_t>(std::clamp<std::int64_t>(snap(kbTick, next, false), 0, pt - 1));
        created.durationTicks = static_cast<std::int32_t>(std::clamp<std::int64_t>(unit(next), 1, pt - created.onsetTicks));
        const auto status = ctl::addEvent(next.lane, created, pt);
        if (status != ctl::EditStatus::Ok)
        {
            announceCursor(status == ctl::EditStatus::Capacity ? "Event limit reached (32)."
                                                               : "Events of one operation cannot overlap.");
            return;
        }
        selectedEvent = created; // set before the commit so the refresh keeps the selection on the new event
        hasSelection = true;
        commit(next);
        selectedIndex = findSelected(draft.lane);
        hasSelection = selectedIndex >= 0;
        if (!hasSelection)
        {
            announceCursor("The event was not created (a lock kept the lane unchanged).");
            return;
        }
        kbTick = std::min<std::int64_t>(pt - 1, static_cast<std::int64_t>(created.onsetTicks) + created.durationTicks);
        if (onSelectionChanged)
            onSelectionChanged();
        setDescription("Event created: " + operationName(lane) + " at " + describePosition(created.onsetTicks, grid)
                       + ", length " + juce::String(created.durationTicks) + " ticks. Arrow keys move it, Delete removes it.");
        lastDescription = getDescription();
        if (onMessage)
            onMessage("Event created: " + operationName(lane) + " at " + describePosition(created.onsetTicks, grid) + ".");
        repaint();
    }
    // Pixel ranges of one event; wrapping events split in two.
    static std::vector<std::pair<int, int>> eventRanges(const ctl::LaneEvent& e, double P, int width)
    {
        std::vector<std::pair<int, int>> ranges;
        // Events saved for a longer phrase wrap into this one, exactly like the audio side.
        const double a = std::fmod(static_cast<double>(e.onsetTicks), P);
        const double b = a + std::min(static_cast<double>(e.durationTicks), P);
        if (b <= P)
            ranges.emplace_back(static_cast<int>(a / P * width), static_cast<int>(b / P * width));
        else
        {
            ranges.emplace_back(static_cast<int>(a / P * width), width);
            ranges.emplace_back(0, static_cast<int>((b - P) / P * width));
        }
        return ranges;
    }
    static std::vector<std::pair<int, int>> regionRanges(const ctl::ProtectedRegion& region, double P, int width)
    {
        ctl::LaneEvent asEvent;
        asEvent.onsetTicks = region.startTicks;
        asEvent.durationTicks = region.lengthTicks;
        return eventRanges(asEvent, P, width);
    }
    void commit(const ctl::SceneControl& next)
    {
        auto settings = access.settings();
        settings.control.lane = next.lane;
        access.setSettings(settings);
        draft = access.settings().control;
        repaint();
    }
};

// ---------------------------------------------------------------------------------------
// Placement tab
// ---------------------------------------------------------------------------------------
class PlacementTab final : public InspectorTab
{
public:
    PlacementTab(GrooveMorphAudioProcessor& p, SceneEditAccess a) : InspectorTab(p, a), canvas(p, a), euclid(p, a)
    {
        enable.setButtonText("Enable phrase placement");
        enable.setTooltip("When on, the scene's effects act only inside the chosen window of the "
                          "phrase (and only on the chosen phrases). Off: effects act on the whole phrase.");
        enable.onClick = [this] { edit([this](auto& s) { s.control.placement.enabled = enable.getToggleState(); }); };
        window.addItemList({"Full phrase", "Custom window", "Last bar"}, 1);
        window.setTooltip("Which part of the phrase is active. Custom uses Start/Span (may wrap past the "
                          "phrase end). Last bar activates only the final bar.");
        window.onChange = [this] {
            edit([this](auto& s) { s.control.placement.window = static_cast<ctl::WindowMode>(window.getSelectedId() - 1); });
        };
        snap.addItemList({"1 per beat", "2 per beat", "3 per beat", "4 per beat", "6 per beat", "8 per beat",
                          "12 per beat", "16 per beat"}, 1);
        snap.setTooltip("Grid used for placing and nudging events (divisions of one meter beat).");
        snap.onChange = [this] {
            edit([this](auto& s) { s.control.placement.subdivision = snapValues[snap.getSelectedId() - 1]; });
        };
        setupLabel(snapLabel, "Snap grid");
        start.setup(*this, "Window start", "Custom window start as a fraction of the phrase (0 = phrase start).", 0, 0.999, 0.001, 3);
        start.commit = [this] { edit([this](auto& s) { s.control.placement.startPhase = static_cast<float>(start.slider.getValue()); }); };
        span.setup(*this, "Window span", "Custom window length as a fraction of the phrase (1 = whole phrase). Start + span may wrap.", 0.001, 1, 0.001, 3);
        span.commit = [this] { edit([this](auto& s) { s.control.placement.spanPhase = static_cast<float>(span.slider.getValue()); }); };
        every.setup(*this, "Every N phrases", "Act on one phrase out of every N (1 = every phrase).", 1, 64, 1, 0);
        every.commit = [this] { edit([this](auto& s) { s.control.placement.every = static_cast<std::uint8_t>(every.slider.getValue()); }); };
        offset.setup(*this, "Phrase offset", "Which phrase of each N-phrase cycle is active (0 = first). Counted from the grid origin.", 0, 63, 1, 0);
        offset.commit = [this] { edit([this](auto& s) { s.control.placement.cadenceOffset = static_cast<std::uint8_t>(offset.slider.getValue()); }); };
        origin.setup(*this, "Phrase origin (beats)", "Shifts the placement timeline against the host/phrase grid, in meter beats. Positive moves events later.", -16, 16, 1.0 / 16.0, 3);
        origin.commit = [this] { edit([this](auto& s) { s.control.placement.originTicks = static_cast<std::int32_t>(std::lround(origin.slider.getValue() * ctl::ticksPerBeat)); }); };
        protect.setup(*this, "Protect downbeat (beats)", "Silences all effects for this many beats at the start of every bar (0 = off), keeping the groove anchor intact.", 0, 4, 0.25, 2);
        protect.commit = [this] { edit([this](auto& s) { s.control.placement.downbeatProtectBeats = static_cast<float>(protect.slider.getValue()); }); };

        modeButton.setButtonText("Use event lanes");
        modeButton.setTooltip("Off: every enabled effect is active across the whole (windowed) phrase. "
                              "On: an effect is active only inside its own events below.");
        modeButton.onClick = [this] { edit([this](auto& s) { s.control.lane.explicitEvents = modeButton.getToggleState(); }); };
        convert.setButtonText("Convert to events");
        convert.setTooltip("Creates one full-phrase event for each enabled effect and switches to event lanes "
                           "(one undo step). Sound is unchanged until you edit the events.");
        convert.onClick = [this] {
            edit([this](auto& s) {
                std::uint32_t mask = 0;
                for (std::size_t i = 0; i < gm::operationCount; ++i)
                    if (s.operations[i].enabled)
                        mask |= 1u << i;
                const auto pt = processor.getGridContext().phraseTicks();
                s.control.lane.count = 0;
                ctl::convertLegacyToEvents(s.control.lane, mask, pt, &s.control.activity.priority);
            });
        };
        removeSelected.setButtonText("Delete event");
        removeSelected.setTooltip("Deletes the selected event (also Delete key or double-click).");
        removeSelected.onClick = [this] { canvas.deleteSelected(); };
        eventDepth.setup(*this, "Event depth", "Scales the operation depth inside the selected event.", 0, 1, 0.01, 2);
        eventDepth.commit = [this] {
            const int i = canvas.selected();
            if (i < 0 || refreshing)
                return;
            edit([this, i](auto& s) {
                if (static_cast<std::size_t>(i) < s.control.lane.count)
                    s.control.lane.events[static_cast<std::size_t>(i)].depth = static_cast<float>(eventDepth.slider.getValue());
            });
        };
        eventSlice.setup(*this, "Event slice (beats)", "0 = use the effect's own slice. Otherwise this event uses its own slice length (1/16 - 4 beats) while it is active.", 0, 4, 1.0 / 16.0, 3);
        eventSlice.slider.textFromValueFunction = [](double v) { return v < 0.001 ? juce::String("effect") : juce::String(v, 3); };
        eventSlice.commit = [this] {
            const int i = canvas.selected();
            if (i < 0 || refreshing) return;
            edit([this, i](auto& s) {
                if (static_cast<std::size_t>(i) < s.control.lane.count)
                    s.control.lane.events[static_cast<std::size_t>(i)].sliceBeats = static_cast<float>(eventSlice.slider.getValue());
            });
        };
        eventPriority.setup(*this, "Event priority", "0-100. When the 'effects at once' limit is exceeded, higher-priority events keep playing (Limits tab).", 0, 100, 1, 0);
        eventPriority.commit = [this] {
            const int i = canvas.selected();
            if (i < 0 || refreshing) return;
            edit([this, i](auto& s) {
                if (static_cast<std::size_t>(i) < s.control.lane.count)
                    s.control.lane.events[static_cast<std::size_t>(i)].priority = static_cast<std::uint8_t>(eventPriority.slider.getValue());
            });
        };
        eventLocked.setButtonText("Lock event");
        eventLocked.setTooltip("A locked event cannot be moved, resized or deleted, and survives generation, morphing and Reduce unchanged.");
        eventLocked.onClick = [this] {
            const int i = canvas.selected();
            if (i < 0 || refreshing)
                return;
            edit([this, i](auto& s) {
                if (static_cast<std::size_t>(i) < s.control.lane.count)
                    s.control.lane.events[static_cast<std::size_t>(i)].locked = eventLocked.getToggleState();
            });
        };
        eventRestart.setButtonText("Restart cycle at start");
        eventRestart.setTooltip("On: the effect's repeat/gate/rate grid restarts at the event onset, so the event always begins at the start of a cycle. Off: the effect keeps the shared beat grid.");
        eventRestart.onClick = [this] {
            const int i = canvas.selected();
            if (i < 0 || refreshing) return;
            edit([this, i](auto& s) {
                if (static_cast<std::size_t>(i) < s.control.lane.count)
                    s.control.lane.events[static_cast<std::size_t>(i)].restartCycle = eventRestart.getToggleState();
            });
        };
        loopLane.addItemList(operationNames, 1);
        loopLane.setSelectedId(1, juce::dontSendNotification);
        loopLane.setTooltip("Lane whose loop length to set (polyrhythm).");
        loopLane.onChange = [this] { refresh(access.settings()); };
        loopBeats.setup(*this, "", "Loop length of the chosen lane's events in meter beats. 0 = follow the phrase. A lane with 3 beats and another with 4 beats drift against each other (polyrhythm).", 0, 64, 1, 0);
        loopBeats.label.setVisible(false);
        loopBeats.slider.textFromValueFunction = [](double v) { return v < 0.5 ? juce::String("phrase") : juce::String(static_cast<int>(v)) + " beats"; };
        loopBeats.commit = [this] {
            const auto lane = static_cast<std::size_t>(juce::jmax(0, loopLane.getSelectedId() - 1));
            edit([this, lane](auto& s) { s.control.lane.loopBeats[lane] = static_cast<std::uint8_t>(loopBeats.slider.getValue()); });
        };
        eventEnabled.setButtonText("Event enabled");
        eventEnabled.setTooltip("Mutes the selected event without deleting it.");
        eventEnabled.onClick = [this] {
            const int i = canvas.selected();
            if (i < 0 || refreshing)
                return;
            edit([this, i](auto& s) {
                if (static_cast<std::size_t>(i) < s.control.lane.count)
                    s.control.lane.events[static_cast<std::size_t>(i)].enabled = eventEnabled.getToggleState();
            });
        };
        canvas.onSelectionChanged = [this] { refresh(access.settings()); };
        canvas.onMessage = [this](const juce::String& m) { message.setText(m, juce::dontSendNotification); };
        setupLabel(message, "");
        message.setColour(juce::Label::textColourId, juce::Colours::orange);
        for (auto* c : std::initializer_list<juce::Component*>{&enable, &window, &snap, &snapLabel, &modeButton, &convert,
                                                               &removeSelected, &eventEnabled, &message, &canvas, &eventRestart, &eventLocked, &loopLane})
            addAndMakeVisible(*c);
        // N03: Euclid generator and chance panel, swapped in over the lower event rows (EuclidPanel.cpp).
        euclidToggle.setButtonText("Euclid / chance");
        euclidToggle.setClickingTogglesState(true);
        euclidToggle.setTooltip("Euclidean pattern generator and per-event / per-lane chance. Shows the panel in place of the event controls below the lanes.");
        euclidToggle.onClick = [this] { euclid.setVisible(euclidToggle.getToggleState()); };
        addAndMakeVisible(euclidToggle);
        addChildComponent(euclid);
    }

    void refresh(const gm::VariationSettings& s) override
    {
        const juce::ScopedValueSetter<bool> guard(refreshing, true);
        const auto& p = s.control.placement;
        const bool locked = s.control.permissions.placementLocked;
        enable.setToggleState(p.enabled, juce::dontSendNotification);
        window.setSelectedId(static_cast<int>(p.window) + 1, juce::dontSendNotification);
        for (int i = 0; i < 8; ++i)
            if (snapValues[i] == p.subdivision)
                snap.setSelectedId(i + 1, juce::dontSendNotification);
        start.set(p.startPhase);
        span.set(p.spanPhase);
        every.set(p.every);
        offset.set(p.cadenceOffset);
        origin.set(static_cast<double>(p.originTicks) / ctl::ticksPerBeat);
        protect.set(p.downbeatProtectBeats);
        modeButton.setToggleState(s.control.lane.explicitEvents, juce::dontSendNotification);
        const bool custom = p.window == ctl::WindowMode::Custom && p.enabled;
        const bool free = !locked;
        start.slider.setEnabled(free && custom);
        span.slider.setEnabled(free && custom);
        every.slider.setEnabled(free && p.enabled);
        offset.slider.setEnabled(free && p.enabled);
        window.setEnabled(free && p.enabled);
        enable.setEnabled(free);
        origin.slider.setEnabled(free);
        protect.slider.setEnabled(free);
        if (static_cast<int>(locked) != lastLocked) // only on change, so canvas feedback ("event limit reached") is not erased
        {
            lastLocked = static_cast<int>(locked);
            message.setText(locked ? "Placement is locked (Generator tab > Locks)." : "", juce::dontSendNotification);
        }
        convert.setEnabled(!s.control.lane.explicitEvents);
        canvas.setSettingsView(s);
        const int sel = canvas.selected();
        const bool haveSel = s.control.lane.explicitEvents && sel >= 0 && static_cast<std::size_t>(sel) < s.control.lane.count;
        removeSelected.setEnabled(haveSel);
        eventDepth.slider.setEnabled(haveSel);
        eventSlice.slider.setEnabled(haveSel);
        eventPriority.slider.setEnabled(haveSel);
        eventRestart.setEnabled(haveSel);
        eventEnabled.setEnabled(haveSel);
        eventLocked.setEnabled(haveSel);
        loopLane.setEnabled(s.control.lane.explicitEvents);
        loopBeats.slider.setEnabled(s.control.lane.explicitEvents);
        loopBeats.set(s.control.lane.loopBeats[static_cast<std::size_t>(juce::jmax(0, loopLane.getSelectedId() - 1))]);
        if (haveSel)
        {
            const auto& e = s.control.lane.events[static_cast<std::size_t>(sel)];
            eventSlice.set(e.sliceBeats);
            eventPriority.set(e.priority);
            eventRestart.setToggleState(e.restartCycle, juce::dontSendNotification);
            eventDepth.set(e.depth);
            eventEnabled.setToggleState(e.enabled, juce::dontSendNotification);
            eventLocked.setToggleState(e.locked, juce::dontSendNotification);
        }
        else if (sel >= 0)
            canvas.select(-1);
        canvas.setSettingsView(s);
        euclid.refresh(s, canvas.selected());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto top = nextRow(area);
        enable.setBounds(top.removeFromLeft(top.getWidth() / 2));
        window.setBounds(top);
        start.layout(nextRow(area), 150);
        span.layout(nextRow(area), 150);
        every.layout(nextRow(area), 150);
        offset.layout(nextRow(area), 150);
        origin.layout(nextRow(area), 150);
        protect.layout(nextRow(area), 150);
        auto snapRow = nextRow(area);
        snapLabel.setBounds(snapRow.removeFromLeft(150));
        snap.setBounds(snapRow.removeFromLeft(130));
        euclidToggle.setBounds(snapRow.removeFromLeft(130).reduced(6, 0));
        auto buttons = nextRow(area);
        modeButton.setBounds(buttons.removeFromLeft(130));
        convert.setBounds(buttons.removeFromLeft(130).reduced(2, 0));
        removeSelected.setBounds(buttons.removeFromLeft(100).reduced(2, 0));
        canvas.setBounds(nextRow(area, 18 + static_cast<int>(gm::operationCount) * 24 + 6));
        euclid.setBounds(area); // covers the event rows below the lanes while shown
        eventDepth.layout(nextRow(area), 150);
        eventSlice.layout(nextRow(area), 150);
        eventPriority.layout(nextRow(area), 150);
        auto flags = nextRow(area);
        eventEnabled.setBounds(flags.removeFromLeft(120));
        eventLocked.setBounds(flags.removeFromLeft(100));
        eventRestart.setBounds(flags);
        auto loop = nextRow(area);
        loopLane.setBounds(loop.removeFromLeft(150));
        loopBeats.slider.setBounds(loop);
        message.setBounds(nextRow(area));
    }

private:
    int lastLocked = -1;
    MenuToggleButton enable, modeButton, eventEnabled;
    MenuComboBox window, snap;
    juce::Label snapLabel, message;
    SliderRow start, span, every, offset, origin, protect, eventDepth, eventSlice, eventPriority, loopBeats;
    MenuToggleButton eventRestart, eventLocked;
    MenuComboBox loopLane;
    juce::TextButton convert, removeSelected, euclidToggle;
    LaneCanvas canvas;
    EuclidPanel euclid;
};

// ---------------------------------------------------------------------------------------
// Generator tab (A03 / C02 / C03 / F08)
// ---------------------------------------------------------------------------------------
class GeneratorTab final : public InspectorTab
{
public:
    GeneratorTab(GrooveMorphAudioProcessor& p, SceneEditAccess a) : InspectorTab(p, a)
    {
        style.addItemList({"Conservative", "Creative", "Experimental"}, 1);
        style.setSelectedId(1, juce::dontSendNotification);
        style.setTooltip("How many operations the generator may change and by how much.");
        setupLabel(styleLabel, "Style");
        setupLabel(seedLabel, "Seed", "Same seed + style + candidate + distance on the same source scene always gives the same result.");
        seedEditor.setInputRestrictions(20, "0123456789");
        seedEditor.setText(juce::String(static_cast<juce::int64>(settings.seed)), false);
        seedEditor.setTooltip("Unsigned 64-bit seed (digits only).");
        seedEditor.onReturnKey = [this] { readSeed(); };
        seedEditor.onFocusLost = [this] { readSeed(); };
        newSeed.setButtonText("New seed");
        newSeed.setTooltip("Picks a fresh random seed and resets the candidate number to 1.");
        newSeed.onClick = [this] {
            settings.seed = static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64());
            settings.candidate = 0;
            seedEditor.setText(juce::String(settings.seed), false);
            updateCandidateLabel();
        };
        distance.setup(*this, "Distance", "How far the candidate moves from the stored scene: 0 = unchanged, 1 = full generated change. Enable flips need >= 0.5.", 0, 1, 0.01, 2);
        distance.slider.setValue(1.0, juce::dontSendNotification);
        distance.commit = [this] { settings.distance = static_cast<float>(distance.slider.getValue()); };
        generate.setButtonText("Generate preview");
        generate.setTooltip("Builds a candidate from the STORED scene and auditions it. Nothing is committed: "
                            "use 'Apply preview' to keep it or 'Revert preview' to discard it. Replaces unapplied preview edits.");
        generate.onClick = [this] { run(); };
        galleryGenerate.setButtonText("Generate 3 options");
        galleryGenerate.setTooltip("Generate three deterministic options from the stored scene. Choose any card to audition it; apply or revert with the preview controls.");
        galleryGenerate.onClick = [this] { runGallery(); };
        generateEvents.setButtonText("Generate event patterns");
        generateEvents.setTooltip("Create seeded rhythmic event lanes as part of each candidate. Replaces unlocked patterns; locked events stay. Generated events obey operation/event locks, Never touch operations and protected regions.");
        generateEvents.onClick = [this] { settings.generateEventPatterns = generateEvents.getToggleState(); };
        for (std::size_t i = 0; i < galleryButtons.size(); ++i)
        {
            galleryButtons[i].setButtonText("Option " + juce::String(i + 1));
            galleryButtons[i].setTooltip("Audition this generated option against the current input. Apply preview to keep it or revert to discard it.");
            galleryButtons[i].setEnabled(false);
            galleryButtons[i].onClick = [this, i] { previewGalleryOption(i); };
        }
        previous.setButtonText("<");
        previous.setTooltip("Previous candidate number for this seed.");
        previous.onClick = [this] { if (settings.candidate > 0) { --settings.candidate; run(); } };
        regenerate.setButtonText("Regenerate >");
        regenerate.setTooltip("Next candidate for the same seed and source scene.");
        regenerate.onClick = [this] { ++settings.candidate; run(); };
        setupLabel(candidateLabel, "");
        setupLabel(resultLabel, "No candidate generated yet.");
        history.setTextWhenNothingSelected("Generation history");
        history.setTooltip("Recent candidates saved on this computer. Selecting one regenerates it exactly "
                           "from its recorded source scene, seed, style, distance and candidate number.");
        history.onChange = [this] { recall(); };
        for (std::size_t i = 0; i < gm::operationCount; ++i)
        {
            protectOp[i].setButtonText(operationShort[static_cast<int>(i)]);
            protectOp[i].setTooltip("Generator never changes this operation for this run.");
            addAndMakeVisible(protectOp[i]);
            protectOp[i].onClick = [this, i] {
                if (protectOp[i].getToggleState()) settings.protectedOperations |= 1u << i;
                else settings.protectedOperations &= ~(1u << i);
            };
        }
        setupLabel(protectLabel, "Never touch");
        setupLabel(locksTitle, "Locks (protect fields)", "A locked field cannot be changed by the generator, by phrase evolution, or by manual edits, whole-scene copies and MIDI controllers while it is locked: edits to it snap back. Locks do not change what you hear by themselves.");
        static const char* lockNames[] = {"Enable", "Depth", "Slice", "Events"};
        static const std::uint8_t lockBits[] = {ctl::lock::enable, ctl::lock::depth, ctl::lock::slice, ctl::lock::events};
        for (std::size_t op = 0; op < gm::operationCount; ++op)
        {
            setupLabel(lockRowLabel[op], operationNames[static_cast<int>(op)]);
            addAndMakeVisible(lockRowLabel[op]);
            for (std::size_t k = 0; k < 4; ++k)
            {
                auto& b = locks[op][k];
                b.setButtonText(lockNames[k]);
                b.setTooltip(juce::String("Lock ") + lockNames[k] + " of " + operationNames[static_cast<int>(op)] + " against generation.");
                b.onClick = [this, op, k] {
                    edit([&](auto& s) {
                        auto& l = s.control.permissions.operationLocks[op];
                        if (locks[op][k].getToggleState()) l |= lockBits[k]; else l &= static_cast<std::uint8_t>(~lockBits[k]);
                    });
                };
                addAndMakeVisible(b);
            }
        }
        lockPlacement.setButtonText("Lock placement");
        lockPlacement.setTooltip("Generation and distance never change the phrase placement window, cadence or origin.");
        lockPlacement.onClick = [this] { edit([this](auto& s) { s.control.permissions.placementLocked = lockPlacement.getToggleState(); }); };
        loadPersistentHistory();
        for (auto* c : std::initializer_list<juce::Component*>{&style, &styleLabel, &seedLabel, &seedEditor, &newSeed, &generate, &galleryGenerate, &generateEvents, &previous,
                                                               &regenerate, &candidateLabel, &resultLabel, &history, &protectLabel,
                                                               &locksTitle, &lockPlacement})
            addAndMakeVisible(*c);
        for (auto& button : galleryButtons)
            addAndMakeVisible(button);
        style.onChange = [this] { settings.style = static_cast<gvar::Style>(style.getSelectedId() - 1); };
        updateCandidateLabel();
    }

    void refresh(const gm::VariationSettings& s) override
    {
        const juce::ScopedValueSetter<bool> guard(refreshing, true);
        for (std::size_t op = 0; op < gm::operationCount; ++op)
        {
            const auto l = s.control.permissions.operationLocks[op];
            locks[op][0].setToggleState((l & ctl::lock::enable) != 0, juce::dontSendNotification);
            locks[op][1].setToggleState((l & ctl::lock::depth) != 0, juce::dontSendNotification);
            locks[op][2].setToggleState((l & ctl::lock::slice) != 0, juce::dontSendNotification);
            locks[op][3].setToggleState((l & ctl::lock::events) != 0, juce::dontSendNotification);
        }
        lockPlacement.setToggleState(s.control.permissions.placementLocked, juce::dontSendNotification);
        distance.set(settings.distance);
        history.setEnabled(!entries.empty());
        for (std::size_t i = 0; i < galleryButtons.size(); ++i)
            galleryButtons[i].setEnabled(galleryEntryValid[i] && galleryEntries[i].scene == access.editingIndex());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto r = nextRow(area);
        styleLabel.setBounds(r.removeFromLeft(60));
        style.setBounds(r.removeFromLeft(150));
        r = nextRow(area);
        seedLabel.setBounds(r.removeFromLeft(60));
        newSeed.setBounds(r.removeFromRight(90));
        seedEditor.setBounds(r.reduced(2, 0));
        distance.layout(nextRow(area), 100);
        r = nextRow(area);
        protectLabel.setBounds(r.removeFromLeft(80));
        for (auto& b : protectOp)
            b.setBounds(r.removeFromLeft(r.getWidth() / (static_cast<int>(protectOp.size()) - static_cast<int>(&b - protectOp.data()))));
        r = nextRow(area, 30);
        generate.setBounds(r.removeFromLeft(150));
        previous.setBounds(r.removeFromLeft(36).reduced(2, 0));
        regenerate.setBounds(r.removeFromLeft(110).reduced(2, 0));
        candidateLabel.setBounds(r);
        r = nextRow(area, 28);
        galleryGenerate.setBounds(r.removeFromLeft(150));
        for (auto& button : galleryButtons)
            button.setBounds(r.removeFromLeft(r.getWidth() / (3 - static_cast<int>(&button - galleryButtons.data()))).reduced(2, 0));
        generateEvents.setBounds(nextRow(area));
        resultLabel.setBounds(nextRow(area, 40));
        history.setBounds(nextRow(area));
        area.removeFromTop(6);
        locksTitle.setBounds(nextRow(area));
        for (std::size_t op = 0; op < gm::operationCount; ++op)
        {
            r = nextRow(area, 24);
            lockRowLabel[op].setBounds(r.removeFromLeft(120));
            for (auto& b : locks[op])
                b.setBounds(r.removeFromLeft(r.getWidth() / (4 - static_cast<int>(&b - locks[op].data()))));
        }
        lockPlacement.setBounds(nextRow(area));
    }

private:
    struct Entry
    {
        gm::VariationSettings source;
        gvar::GeneratorSettings settings;
        std::size_t scene = 0;
    };

    gvar::GeneratorSettings settings;
    std::deque<Entry> entries;
    std::array<Entry, 3> galleryEntries{};
    std::array<bool, 3> galleryEntryValid{};
    MenuComboBox style, history;
    juce::Label styleLabel, seedLabel, protectLabel, locksTitle, candidateLabel, resultLabel;
    juce::TextEditor seedEditor;
    juce::TextButton newSeed, generate, galleryGenerate, previous, regenerate;
    MenuToggleButton generateEvents;
    std::array<juce::TextButton, 3> galleryButtons;
    SliderRow distance;
    std::array<MenuToggleButton, gm::operationCount> protectOp;
    std::array<juce::Label, gm::operationCount> lockRowLabel;
    std::array<std::array<MenuToggleButton, 4>, gm::operationCount> locks;
    MenuToggleButton lockPlacement;

    void readSeed()
    {
        // Digits only, any length up to 20 (unsigned 64-bit; larger values wrap).
        const auto text = seedEditor.getText().trim();
        std::uint64_t value = 0;
        for (auto ch : text)
            if (ch >= '0' && ch <= '9')
                value = value * 10u + static_cast<std::uint64_t>(ch - '0');
        const std::uint64_t newSeed = text.isEmpty() ? 1ull : value;
        if (newSeed != settings.seed)
            settings.candidate = 0; // a new seed starts its own sequence; unchanged text keeps the candidate
        settings.seed = newSeed;
        if (text.isEmpty())
            seedEditor.setText(juce::String(static_cast<juce::int64>(newSeed)), false);
        updateCandidateLabel();
    }
    void updateCandidateLabel()
    {
        candidateLabel.setText("Candidate #" + juce::String(settings.candidate + 1), juce::dontSendNotification);
    }
    void describe(const gvar::Candidate& c)
    {
        juce::StringArray changed;
        for (std::size_t i = 0; i < gm::operationCount; ++i)
            if ((c.changedOperations & (1u << i)) != 0)
                changed.add(operationNames[static_cast<int>(i)]);
        juce::String text = changed.isEmpty() && !c.changedEvents
                                ? "Candidate changes nothing (try Regenerate, a bolder style or more distance)."
                                : changed.isEmpty() ? "Generated rhythmic event pattern."
                                                    : "Changed: " + changed.joinIntoString(", ");
        if (c.changedEvents && !changed.isEmpty())
            text << "  |  rhythmic event pattern changed";
        if (c.permissionRestores > 0)
            text << "  |  locks restored " << juce::String(c.permissionRestores) << " field(s)";
        text << "  |  Previewing: use Apply preview / Revert preview.";
        resultLabel.setText(text, juce::dontSendNotification);
    }
    void run()
    {
        readSeedQuiet();
        configureEventGeneration();
        const auto source = access.storedSettings();
        const auto candidate = gvar::buildCandidate(source, settings);
        entries.push_front({source, settings, access.editingIndex()});
        if (entries.size() > 24)
            entries.pop_back();
        savePersistentHistory();
        rebuildHistory();
        describe(candidate);
        updateCandidateLabel();
        access.previewCandidate(candidate.settings);
    }

    void runGallery()
    {
        readSeedQuiet();
        configureEventGeneration();
        const auto source = access.storedSettings();
        galleryEntryValid.fill(true);
        for (std::size_t i = 0; i < galleryEntries.size(); ++i)
        {
            auto candidateSettings = settings;
            candidateSettings.candidate = settings.candidate + static_cast<std::uint32_t>(i);
            galleryEntries[i] = {source, candidateSettings, access.editingIndex()};
            const auto candidate = gvar::buildCandidate(source, candidateSettings);
            entries.push_front(galleryEntries[i]);
            galleryButtons[i].setEnabled(true);
            unsigned changedCount = 0;
            for (auto mask = candidate.changedOperations; mask != 0; mask >>= 1u)
                changedCount += mask & 1u;
            galleryButtons[i].setButtonText("Option " + juce::String(i + 1) + " · #" + juce::String(candidateSettings.candidate + 1)
                                             + " · " + juce::String(changedCount) + " ops"
                                             + (candidate.changedEvents ? " + pattern" : ""));
            if (entries.size() > 24)
                entries.pop_back();
        }
        settings.candidate += static_cast<std::uint32_t>(galleryEntries.size());
        rebuildHistory();
        savePersistentHistory();
        previewGalleryOption(0);
    }

    void previewGalleryOption(std::size_t index)
    {
        if (index >= galleryEntries.size() || !galleryEntryValid[index])
            return;
        const auto& entry = galleryEntries[index];
        if (entry.scene != access.editingIndex())
        {
            resultLabel.setText("That option belongs to scene " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + entry.scene))
                                    + ". Select that scene in the Edit combo first.", juce::dontSendNotification);
            return;
        }
        const auto candidate = gvar::buildCandidate(entry.source, entry.settings);
        describe(candidate);
        resultLabel.setText("Auditioning option " + juce::String(index + 1) + ". " + resultLabel.getText(), juce::dontSendNotification);
        settings.candidate = entry.settings.candidate;
        updateCandidateLabel();
        access.previewCandidate(candidate.settings);
    }
    void readSeedQuiet()
    {
        const auto before = settings.candidate;
        readSeed();
        settings.candidate = before;
    }
    void rebuildHistory()
    {
        history.clear(juce::dontSendNotification);
        int id = 1;
        for (const auto& e : entries)
        {
            history.addItem(juce::String("#") + juce::String(id) + "  scene " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + e.scene))
                                + "  seed " + juce::String(e.settings.seed) + "  cand " + juce::String(e.settings.candidate + 1)
                                + "  " + juce::StringArray{"Cons", "Creat", "Exper"}[static_cast<int>(e.settings.style)]
                                + "  d=" + juce::String(e.settings.distance, 2), id);
            ++id;
        }
    }

    juce::File historyFile() const
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("GrooveMorph").getChildFile("generator-history.txt");
    }

    void savePersistentHistory() const
    {
        std::ostringstream output;
        output << "groovemorph-generator-history 2\n";
        output << std::setprecision(9);
        for (const auto& entry : entries)
        {
            gm::scene::SceneBank sourceBank;
            sourceBank.setVariationSettings(0, entry.source);
            const auto sourceText = gm::scene::SceneSerializer::serialize(sourceBank);
            output << "entry " << entry.scene << ' ' << entry.settings.seed << ' '
                   << static_cast<int>(entry.settings.style) << ' ' << entry.settings.distance << ' '
                   << entry.settings.candidate << ' ' << entry.settings.protectedOperations << ' '
                   << (entry.settings.generateEventPatterns ? 1 : 0) << ' ' << entry.settings.phraseTicks << '\n'
                   << sourceText << "end-entry\n";
        }
        const auto file = historyFile();
        if (!file.getParentDirectory().createDirectory().failed())
            file.replaceWithText(juce::String(output.str()));
    }

    void loadPersistentHistory()
    {
        const auto file = historyFile();
        if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > 1024 * 1024)
            return;
        const auto content = file.loadFileAsString().toStdString();
        std::istringstream input(content);
        std::string line;
        if (!std::getline(input, line) || line != "groovemorph-generator-history 2")
            return;
        while (entries.size() < 24 && std::getline(input, line))
        {
            if (line.empty())
                continue;
            Entry entry;
            int styleIndex = 0;
            int generateEventsValue = 0;
            std::istringstream header(line);
            std::string marker;
            if (!(header >> marker >> entry.scene >> entry.settings.seed >> styleIndex >> entry.settings.distance
                         >> entry.settings.candidate >> entry.settings.protectedOperations >> generateEventsValue
                         >> entry.settings.phraseTicks)
                || marker != "entry" || entry.scene >= gm::variationCount || styleIndex < 0 || styleIndex > 2
                || (generateEventsValue != 0 && generateEventsValue != 1)
                || entry.settings.phraseTicks <= 0 || !std::isfinite(entry.settings.distance))
                break;
            entry.settings.style = static_cast<gvar::Style>(styleIndex);
            entry.settings.generateEventPatterns = generateEventsValue != 0;
            std::string sourceText, sourceLine;
            bool ended = false;
            while (std::getline(input, sourceLine))
            {
                if (sourceLine == "end-entry")
                {
                    ended = true;
                    break;
                }
                sourceText += sourceLine;
                sourceText.push_back('\n');
            }
            if (!ended)
                break;
            const auto sourceBank = gm::scene::SceneSerializer::deserialize(sourceText);
            if (sourceBank.report.status == gm::scene::LoadStatus::Defaulted)
                continue;
            entry.source = sourceBank.bank.variation(0).settings;
            entries.push_back(std::move(entry));
        }
        rebuildHistory();
    }

    void configureEventGeneration()
    {
        settings.generateEventPatterns = generateEvents.getToggleState();
        settings.phraseTicks = processor.getGridContext().phraseTicks();
    }
    void recall()
    {
        const int id = history.getSelectedId();
        if (id < 1 || static_cast<std::size_t>(id) > entries.size())
            return;
        const auto entry = entries[static_cast<std::size_t>(id - 1)];
        if (entry.scene != access.editingIndex())
        {
            resultLabel.setText("That entry belongs to scene " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + entry.scene))
                                    + ". Select it in the Edit combo first.", juce::dontSendNotification);
            return;
        }
        const auto candidate = gvar::buildCandidate(entry.source, entry.settings);
        describe(candidate);
        resultLabel.setText("Recalled history #" + juce::String(id) + ". " + resultLabel.getText(), juce::dontSendNotification);
        access.previewCandidate(candidate.settings);
    }
};

// ---------------------------------------------------------------------------------------
// Scenes tab (A04)
// ---------------------------------------------------------------------------------------
class ScenesTab final : public InspectorTab
{
public:
    ScenesTab(GrooveMorphAudioProcessor& p, SceneEditAccess a) : InspectorTab(p, a)
    {
        info.setJustificationType(juce::Justification::topLeft);
        info.setColour(juce::Label::textColourId, juce::Colours::white);
        target.addItemList({"A", "B", "C", "D"}, 1);
        target.setSelectedId(2, juce::dontSendNotification);
        target.setTooltip("Destination scene for Duplicate.");
        duplicate.setButtonText("Duplicate edited scene to target");
        duplicate.setTooltip("Copies the edited scene completely (effects, placement, events, locks and name) into the target. One undo step. Disabled during a preview.");
        duplicate.onClick = [this] {
            const auto from = access.editingIndex();
            const auto to = static_cast<std::size_t>(juce::jmax(0, target.getSelectedId() - 1));
            juce::String text = "Nothing to duplicate (same scene, identical scenes or preview active).";
            if (processor.duplicateVariation(from, to))
            {
                // C03 locks of the destination can keep fields unchanged; do not claim a full copy then.
                const auto bank = processor.getSceneBank();
                text = bank.variation(to) == bank.variation(from)
                    ? juce::String("Duplicated.")
                    : "Duplicated, but locked fields of scene " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + to))
                          + " were kept (Gen tab > Locks).";
            }
            message.setText(text, juce::dontSendNotification);
        };
        resetScene.setButtonText("Reset edited scene to factory");
        resetScene.setTooltip("Restores the factory name and settings of the edited scene. One undo step. Keeps the MIDI note. Disabled during a preview.");
        resetScene.onClick = [this] {
            const auto index = access.editingIndex();
            processor.resetVariation(index);
            const bool exact = processor.getSceneBank().variation(index) == gm::scene::SceneBank::defaultVariation(index);
            message.setText(exact ? "Scene reset (Undo available)." : "Scene reset, but locked fields were kept (Gen tab > Locks).",
                            juce::dontSendNotification);
        };
        op.addItemList(operationNames, 1);
        op.setSelectedId(1, juce::dontSendNotification);
        op.setTooltip("Effect to reset.");
        resetOp.setButtonText("Reset selected effect");
        resetOp.setTooltip("Resets the chosen effect of the edited scene to off / depth 1 / slice 0.25 beat.");
        resetOp.onClick = [this] {
            const auto index = static_cast<std::size_t>(juce::jmax(0, op.getSelectedId() - 1));
            edit([index](auto& s) { s.operations[index] = gm::OperationSettings{}; });
            const bool exact = gm::scene::sameSettings(access.settings().operations[index], gm::OperationSettings{});
            message.setText(exact ? "Effect reset." : "Effect not fully reset: locked fields were kept (Gen tab > Locks).",
                            juce::dontSendNotification);
        };
        clearPlacement.setButtonText("Clear placement and events");
        clearPlacement.setTooltip("Returns the edited scene to uniform whole-phrase behavior (keeps effect settings, locks and limits).");
        clearPlacement.onClick = [this] {
            edit([](auto& s) { s.control.placement = ctl::PlacementConfig{}; s.control.lane = ctl::EventLane{}; });
            ctl::SceneControl remaining;
            remaining.placement = access.settings().control.placement;
            remaining.lane = access.settings().control.lane;
            message.setText(ctl::isDefault(remaining) ? "Placement cleared."
                                                       : "Placement not fully cleared: a lock kept part of it (Gen tab > Locks).",
                            juce::dontSendNotification);
        };
        for (auto* c : std::initializer_list<juce::Component*>{&info, &target, &duplicate, &resetScene, &op, &resetOp, &clearPlacement, &message})
            addAndMakeVisible(*c);
        message.setColour(juce::Label::textColourId, juce::Colours::orange);
    }
    void refresh(const gm::VariationSettings&) override
    {
        const auto bank = processor.getSceneBank();
        const auto editing = access.editingIndex();
        const auto active = processor.getActiveVariation();
        juce::String text;
        text << "Editing scene " << juce::String::charToString(static_cast<juce::juce_wchar>('A' + editing))
             << " (" << juce::String(bank.variation(editing).name) << ")\n"
             << "Sounding scene " << juce::String::charToString(static_cast<juce::juce_wchar>('A' + active)) << "\n"
             << (editing == active ? "Edited scene is the sounding scene." : "Edited scene is NOT sounding; use Preview to hear it.");
        info.setText(text, juce::dontSendNotification);
        const bool free = !access.previewing();
        duplicate.setEnabled(free && static_cast<std::size_t>(target.getSelectedId() - 1) != editing);
        resetScene.setEnabled(free);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        info.setBounds(nextRow(area, 64));
        auto r = nextRow(area);
        target.setBounds(r.removeFromLeft(70));
        duplicate.setBounds(r.reduced(4, 0));
        resetScene.setBounds(nextRow(area));
        r = nextRow(area);
        op.setBounds(r.removeFromLeft(150));
        resetOp.setBounds(r.reduced(4, 0));
        clearPlacement.setBounds(nextRow(area));
        message.setBounds(nextRow(area));
    }
private:
    juce::Label info, message;
    MenuComboBox target, op;
    juce::TextButton duplicate, resetScene, resetOp, clearPlacement;
};

// ---------------------------------------------------------------------------------------
// Morph & Return tab (F02 / C05)
// ---------------------------------------------------------------------------------------
class MorphTab final : public InspectorTab
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    MorphTab(GrooveMorphAudioProcessor& p, SceneEditAccess a) : InspectorTab(p, a)
    {
        setupLabel(morphTitle, "Scene morph (host automatable)");
        setupLabel(morphTargetLabel, "Morph toward");
        morphTarget.addItemList({"Off", "A", "B", "C", "D"}, 1);
        morphTarget.setTooltip("Blends the sounding scene toward this scene. Off disables morphing.");
        setupLabel(morphAmountLabel, "Amount");
        morphAmount.setSliderStyle(juce::Slider::LinearHorizontal);
        morphAmount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
        morphAmount.setNumDecimalPlacesToDisplay(2);
        morphAmount.setTooltip("0 = sounding scene only, 1 = target scene. Effects present in only one scene fade in/out; shared effects interpolate depth and slice.");
        setupLabel(note, "Morph is applied on top of phrase placement of the sounding scene. It does not edit stored scenes.");
        note.setJustificationType(juce::Justification::topLeft);

        setupLabel(returnTitle, "Automatic return (host automatable)");
        setupLabel(returnBarsLabel, "Return after");
        returnBars.setSliderStyle(juce::Slider::LinearHorizontal);
        returnBars.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
        returnBars.setNumDecimalPlacesToDisplay(0);
        returnBars.setTooltip("0 = off. After a scene switch, switch back after this many bars. Lands on the bar line. A new request, Cancel or a seek cancels it.");
        setupLabel(returnUnitLabel, "Unit");
        returnUnit.addItemList({"Bars", "Phrases"}, 1);
        returnUnit.setTooltip("Count the return delay in bars or in whole phrases (phrase length comes from the timing settings).");
        setupLabel(returnTargetLabel, "Return to");
        returnTarget.addItemList({"Previous scene", "A", "B", "C", "D", "Dry (no effects)"}, 1);
        returnTarget.setTooltip("Scene to return to. Previous returns to the scene that was sounding before the switch. Dry turns all effects off at the return point until you pick a scene.");
        setupLabel(returnStatus, "");

        for (auto* c : std::initializer_list<juce::Component*>{&morphTitle, &morphTargetLabel, &morphTarget, &morphAmountLabel,
                                                               &morphAmount, &note, &returnTitle, &returnBarsLabel, &returnBars,
                                                               &returnTargetLabel, &returnTarget, &returnStatus, &returnUnitLabel, &returnUnit})
            addAndMakeVisible(*c);
        auto& apvts = processor.parameters;
        attachments.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::morphTargetId, morphTarget));
        linkParam(morphTarget, GrooveMorphAudioProcessor::morphTargetId);
        attachments.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::morphAmountId, morphAmount));
        linkParam(morphAmount, GrooveMorphAudioProcessor::morphAmountId);
        attachments.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::autoReturnBarsId, returnBars));
        linkParam(returnBars, GrooveMorphAudioProcessor::autoReturnBarsId);
        attachments.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::autoReturnTargetId, returnTarget));
        linkParam(returnTarget, GrooveMorphAudioProcessor::autoReturnTargetId);
        attachments.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::autoReturnUnitId, returnUnit));
        linkParam(returnUnit, GrooveMorphAudioProcessor::autoReturnUnitId);
    }
    void refresh(const gm::VariationSettings&) override
    {
        morphAmount.setEnabled(morphTarget.getSelectedId() > 1); // nothing to morph toward when the target is Off
        returnTarget.setEnabled(returnBars.getValue() > 0.0);
        returnUnit.setEnabled(returnBars.getValue() > 0.0);
        const auto t = processor.getAutoReturnTarget();
        const auto dry = processor.getDryReturnState();
        returnStatus.setText(dry == 2 ? "Dry: effects are off until you pick a scene."
                             : dry == 1 ? "Return to dry pending."
                             : t >= 0 ? "Return pending to scene " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + t))
                                      : "No return pending.",
                             juce::dontSendNotification);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        morphTitle.setBounds(nextRow(area));
        auto r = nextRow(area);
        morphTargetLabel.setBounds(r.removeFromLeft(120));
        morphTarget.setBounds(r.removeFromLeft(120));
        r = nextRow(area);
        morphAmountLabel.setBounds(r.removeFromLeft(120));
        morphAmount.setBounds(r);
        note.setBounds(nextRow(area, 44));
        area.removeFromTop(10);
        returnTitle.setBounds(nextRow(area));
        r = nextRow(area);
        returnBarsLabel.setBounds(r.removeFromLeft(120));
        returnBars.setBounds(r);
        r = nextRow(area);
        returnUnitLabel.setBounds(r.removeFromLeft(120));
        returnUnit.setBounds(r.removeFromLeft(150));
        r = nextRow(area);
        returnTargetLabel.setBounds(r.removeFromLeft(120));
        returnTarget.setBounds(r.removeFromLeft(150));
        returnStatus.setBounds(nextRow(area));
    }
private:
    juce::Label morphTitle, morphTargetLabel, morphAmountLabel, note, returnTitle, returnBarsLabel, returnTargetLabel, returnStatus, returnUnitLabel;
    MenuComboBox morphTarget, returnTarget, returnUnit;
    MenuSlider morphAmount, returnBars;
    struct AttachmentList
    {
        std::vector<std::unique_ptr<SliderAttachment>> sliders;
        std::vector<std::unique_ptr<ComboAttachment>> combos;
        void push_back(std::unique_ptr<SliderAttachment> a) { sliders.push_back(std::move(a)); }
        void push_back(std::unique_ptr<ComboAttachment> a) { combos.push_back(std::move(a)); }
    } attachments;
};

// ---------------------------------------------------------------------------------------
// Limits tab (C06)
// ---------------------------------------------------------------------------------------
class LimitsTab final : public InspectorTab
{
public:
    LimitsTab(GrooveMorphAudioProcessor& p, SceneEditAccess a) : InspectorTab(p, a)
    {
        enable.setButtonText("Limit scheduled effects at once");
        enable.setTooltip("Keeps only the highest-priority gated-on effects at any moment. Limits SCHEDULED effects, not CPU load or loudness.");
        enable.onClick = [this] { edit([this](auto& s) { s.control.activity.enabled = enable.getToggleState(); }); };
        maxSlider.setup(*this, "Maximum at once", "1-7 effects may be active together. Only enabled effects count.", 1, static_cast<double>(gm::operationCount), 1, 0);
        maxSlider.commit = [this] { edit([this](auto& s) { s.control.activity.maxConcurrent = static_cast<std::uint8_t>(maxSlider.slider.getValue()); }); };
        for (std::size_t i = 0; i < gm::operationCount; ++i)
        {
            prio[i].setup(*this, operationNames[static_cast<int>(i)] + " priority", "0-100. Higher priority survives when the limit is exceeded; ties keep the earlier effect in chain order.", 0, 100, 1, 0);
            prio[i].commit = [this, i] { edit([this, i](auto& s) { s.control.activity.priority[i] = static_cast<std::uint8_t>(prio[i].slider.getValue()); }); };
        }
        spacing.setup(*this, "Min event spacing (beats)", "Reduce action: event starts must be at least this far apart (0 = no limit).", 0, 4, 1.0 / 16.0, 3);
        spacing.commit = [this] { edit([this](auto& s) { s.control.activity.minSpacingBeats = static_cast<float>(spacing.slider.getValue()); }); };
        coverage.setup(*this, "Max phrase coverage", "Reduce action: the union of event lengths may cover at most this fraction of the phrase.", 0, 1, 0.01, 2);
        coverage.commit = [this] { edit([this](auto& s) { s.control.activity.maxCoverage = static_cast<float>(coverage.slider.getValue()); }); };
        analyze.setButtonText("Analyze");
        analyze.setTooltip("Shows what Reduce would keep and drop. Changes nothing.");
        analyze.onClick = [this] { run(false); };
        reduce.setButtonText("Reduce events");
        reduce.setTooltip("Drops lower-priority events until the spacing and coverage limits hold. Events of lanes with a locked Events bit are kept. One undo step.");
        reduce.onClick = [this] { run(true); };
        regionSelect.addItemList({"Region 1", "Region 2", "Region 3", "Region 4", "Region 5", "Region 6", "Region 7", "Region 8"}, 1);
        regionSelect.setSelectedId(1, juce::dontSendNotification);
        regionSelect.setTooltip("Protected regions: no effect sounds inside them (placed on the phrase, in beats).");
        regionSelect.onChange = [this] { refresh(access.settings()); };
        regionAdd.setButtonText("Add region");
        regionAdd.setTooltip("Adds a protected region (first free slot; starts at the phrase start, one beat long).");
        regionAdd.onClick = [this] {
            edit([this](auto& s) {
                auto& pl = s.control.placement;
                if (pl.regionCount < ctl::maxProtectedRegions)
                    pl.regions[pl.regionCount++] = ctl::ProtectedRegion{0, ctl::ticksPerBeat};
            });
            if (const auto count = access.settings().control.placement.regionCount; count > 0)
                regionSelect.setSelectedId(static_cast<int>(count), juce::dontSendNotification); // select the new region
        };
        regionRemove.setButtonText("Remove region");
        regionRemove.setTooltip("Removes the selected protected region.");
        regionRemove.onClick = [this] {
            const auto idx = static_cast<std::size_t>(juce::jmax(0, regionSelect.getSelectedId() - 1));
            edit([idx](auto& s) {
                auto& pl = s.control.placement;
                if (idx >= pl.regionCount)
                    return;
                for (std::size_t i = idx; i + 1 < pl.regionCount; ++i)
                    pl.regions[i] = pl.regions[i + 1];
                pl.regions[--pl.regionCount] = ctl::ProtectedRegion{};
            });
        };
        regionStart.setup(*this, "Region start (beats)", "Start of the selected protected region, in meter beats from the phrase start.", 0, 64, 1.0 / 16.0, 3);
        regionStart.commit = [this] { editRegion(true); };
        regionLength.setup(*this, "Region length (beats)", "Length of the selected protected region, in meter beats.", 1.0 / 16.0, 64, 1.0 / 16.0, 3);
        regionLength.commit = [this] { editRegion(false); };
        scaleDouble.setButtonText("Events x2");
        scaleDouble.setTooltip("Doubles the position and length of every unlocked event (e.g. after doubling the phrase). Events that would fall past the phrase end are dropped. One undo step.");
        scaleDouble.onClick = [this] { scale(2, 1); };
        scaleHalf.setButtonText("Events /2");
        scaleHalf.setTooltip("Halves the position and length of every unlocked event. One undo step.");
        scaleHalf.onClick = [this] { scale(1, 2); };
        scaleResult.setColour(juce::Label::textColourId, juce::Colours::orange);
        setupLabel(note, "'Effects at once' is applied live. Spacing and coverage are explicit actions: they edit the event list only when you press Reduce (Undo restores).");
        note.setJustificationType(juce::Justification::topLeft);
        result.setColour(juce::Label::textColourId, juce::Colours::orange);
        result.setJustificationType(juce::Justification::topLeft);
        for (auto* c : std::initializer_list<juce::Component*>{&regionSelect, &regionAdd, &regionRemove, &scaleDouble, &scaleHalf, &scaleResult})
            addAndMakeVisible(*c);
        addAndMakeVisible(enable);
        addAndMakeVisible(note);
        addAndMakeVisible(analyze);
        addAndMakeVisible(reduce);
        addAndMakeVisible(result);
    }
    void refresh(const gm::VariationSettings& s) override
    {
        const juce::ScopedValueSetter<bool> guard(refreshing, true);
        enable.setToggleState(s.control.activity.enabled, juce::dontSendNotification);
        maxSlider.set(s.control.activity.maxConcurrent);
        for (std::size_t i = 0; i < gm::operationCount; ++i)
            prio[i].set(s.control.activity.priority[i]);
        maxSlider.slider.setEnabled(s.control.activity.enabled);
        for (auto& p : prio)
            p.slider.setEnabled(s.control.activity.enabled);
        spacing.set(s.control.activity.minSpacingBeats);
        coverage.set(s.control.activity.maxCoverage);
        reduce.setEnabled(s.control.lane.explicitEvents && s.control.lane.count > 0);
        analyze.setEnabled(reduce.isEnabled());
        const auto& pl = s.control.placement;
        const bool placementFree = !s.control.permissions.placementLocked;
        const auto idx = static_cast<std::size_t>(juce::jmax(0, regionSelect.getSelectedId() - 1));
        const bool haveRegion = idx < pl.regionCount;
        regionAdd.setEnabled(placementFree && pl.regionCount < ctl::maxProtectedRegions);
        regionRemove.setEnabled(placementFree && haveRegion);
        regionStart.slider.setEnabled(placementFree && haveRegion);
        regionLength.slider.setEnabled(placementFree && haveRegion);
        if (haveRegion)
        {
            regionStart.set(static_cast<double>(pl.regions[idx].startTicks) / ctl::ticksPerBeat);
            regionLength.set(static_cast<double>(pl.regions[idx].lengthTicks) / ctl::ticksPerBeat);
        }
        scaleDouble.setEnabled(s.control.lane.explicitEvents && s.control.lane.count > 0);
        scaleHalf.setEnabled(scaleDouble.isEnabled());
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        enable.setBounds(nextRow(area));
        maxSlider.layout(nextRow(area), 150);
        for (auto& p : prio)
            p.layout(nextRow(area), 190);
        spacing.layout(nextRow(area), 190);
        coverage.layout(nextRow(area), 190);
        auto buttons = nextRow(area, 28);
        analyze.setBounds(buttons.removeFromLeft(100));
        reduce.setBounds(buttons.removeFromLeft(130).reduced(4, 0));
        scaleDouble.setBounds(buttons.removeFromLeft(100).reduced(4, 0));
        scaleHalf.setBounds(buttons.removeFromLeft(100).reduced(4, 0));
        result.setBounds(nextRow(area, 36));
        auto regionRow = nextRow(area, 28);
        regionSelect.setBounds(regionRow.removeFromLeft(110));
        regionAdd.setBounds(regionRow.removeFromLeft(110).reduced(4, 0));
        regionRemove.setBounds(regionRow.removeFromLeft(120).reduced(4, 0));
        regionStart.layout(nextRow(area), 190);
        regionLength.layout(nextRow(area), 190);
        scaleResult.setBounds(nextRow(area));
        note.setBounds(nextRow(area, 52));
    }
private:
    void editRegion(bool isStart)
    {
        if (refreshing)
            return;
        const auto idx = static_cast<std::size_t>(juce::jmax(0, regionSelect.getSelectedId() - 1));
        edit([this, idx, isStart](auto& s) {
            auto& pl = s.control.placement;
            if (idx >= pl.regionCount)
                return;
            if (isStart)
                pl.regions[idx].startTicks = static_cast<std::int32_t>(std::lround(regionStart.slider.getValue() * ctl::ticksPerBeat));
            else
                pl.regions[idx].lengthTicks = static_cast<std::int32_t>(std::lround(regionLength.slider.getValue() * ctl::ticksPerBeat));
        });
    }
    void scale(int numerator, int denominator)
    {
        auto settings = access.settings();
        const auto report = ctl::scaleEvents(settings.control.lane, numerator, denominator,
                                             processor.getGridContext().phraseTicks(), settings.control.permissions);
        scaleResult.setText("Scaled " + juce::String(report.scaled) + ", dropped " + juce::String(report.dropped)
                                + ", locked kept " + juce::String(report.skippedLocked),
                            juce::dontSendNotification);
        access.setSettings(settings);
    }
    void run(bool apply)
    {
        auto settings = access.settings();
        const auto phraseTicks = processor.getGridContext().phraseTicks();
        auto lane = settings.control.lane;
        const auto report = ctl::reduceEvents(lane, settings.control.activity, settings.control.permissions, phraseTicks, apply);
        if (report.status == ctl::ReduceStatus::ConflictLocked)
        {
            result.setText("Conflict: locked events alone break the limits. Nothing changed.", juce::dontSendNotification);
            return;
        }
        juce::String text = juce::String(apply ? "Reduced: " : "Would keep ") + juce::String(report.kept) + ", drop "
                            + juce::String(report.dropped) + " | coverage " + juce::String(report.coverage * 100.0f, 0)
                            + "% | peak " + juce::String(report.peakConcurrent) + " effects at once";
        result.setText(text, juce::dontSendNotification);
        if (apply && report.dropped > 0)
        {
            settings.control.lane = lane;
            access.setSettings(settings);
        }
    }
    MenuToggleButton enable;
    juce::Label note, result, scaleResult;
    juce::TextButton analyze, reduce, regionAdd, regionRemove, scaleDouble, scaleHalf;
    MenuComboBox regionSelect;
    SliderRow regionStart, regionLength;
    SliderRow spacing, coverage;
    SliderRow maxSlider;
    std::array<SliderRow, gm::operationCount> prio;
};


// ---------------------------------------------------------------------------------------
// Input tab: source, loop capture/import (F09), band split (F03/F10), audio trigger (F07), MIDI (F12)
// ---------------------------------------------------------------------------------------
class InputTab final : public InspectorTab
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    InputTab(GrooveMorphAudioProcessor& p, SceneEditAccess a) : InspectorTab(p, a)
    {
        setupLabel(sourceTitle, "Input source");
        source.addItemList({"Live input", "Demo drum loop", "Captured / imported loop"}, 1);
        source.setTooltip("What feeds the effects. The demo and loop sources replace the input while the transport plays (Standalone: the free-running clock). Not saved with the project.");
        source.onChange = [this] { if (!refreshing) processor.setSourceMode(source.getSelectedId() - 1); };
        bars.addItemList({"Auto bars", "1 bar", "2 bars", "4 bars", "8 bars"}, 1);
        bars.setSelectedId(1, juce::dontSendNotification);
        bars.setTooltip("Length of a captured or imported loop in bars (uses the current beats-per-bar). Auto works the bar count out from the file length so that its tempo is closest to the current tempo. For a capture, Auto records 2 bars. If you know the loop's tempo, type it in the BPM box instead: that is the most reliable way to import.");
        loopBpmField.setInputRestrictions(7, "0123456789.");
        loopBpmField.setTextToShowWhenEmpty("BPM", juce::Colours::grey);
        loopBpmField.setTooltip("The loop's own tempo in BPM (20-400), for imports. Leave empty to work it out automatically. The loop then plays at its original speed when the plugin tempo equals this value, and proportionally faster or slower at other tempos (unless 'Stretch to host tempo' is on, pitch follows the tempo).");
        matchTempo.setButtonText("Set tempo to loop");
        matchTempo.setTooltip("Sets the manual tempo to the tempo of the loaded loop so it plays at its original speed (when Timing method is Sample Clock, or in Standalone).");
        matchTempo.onClick = [this] { message.setText(processor.matchTempoToLoop() ? "Tempo set to the loop's tempo." : "No loop loaded, or its tempo is outside the tempo range.", juce::dontSendNotification); };
        capture.setButtonText("Capture");
        capture.setTooltip("Records the signal entering the effects (live input or demo) for the chosen number of bars, starting on the next bar line while the transport plays. Then select 'Captured / imported loop'.");
        capture.onClick = [this] {
            if (processor.getLoopCaptureState() != groovemorph::core::LoopCapture::State::Idle)
                processor.cancelLoopCapture();
            else if (!processor.armLoopCapture(barChoice() == 0 ? 2 : barChoice()))
                message.setText("Capture could not start.", juce::dontSendNotification);
        };
        importButton.setButtonText("Import file...");
        importButton.setTooltip("Loads a WAV, AIFF, FLAC or OGG file of up to 20 seconds as a loop. Its tempo comes from the BPM box if filled in, otherwise from the bars choice (Auto works it out). It plays locked to the beat grid at the plugin tempo; pitch follows tempo unless 'Stretch to host tempo' is on.");
        importButton.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Choose an audio loop", juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.ogg");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this](const juce::FileChooser& fc) {
                                     const auto file = fc.getResult();
                                     if (file == juce::File{})
                                         return;
                                     const auto result = processor.importLoopFile(file, barChoice(), loopBpmField.getText().getDoubleValue());
                                     message.setText(result.wasOk() ? "Imported " + file.getFileName() + ": " + juce::String(processor.getLoopBeats(), 2)
                                                                          + " beats = " + juce::String(processor.getLoopBpm(), 1) + " BPM. Plays at original speed at that tempo."
                                                                    : result.getErrorMessage(),
                                                     juce::dontSendNotification);
                                     if (result.wasOk())
                                     {
                                         processor.setSourceMode(2);
                                         source.setSelectedId(3, juce::dontSendNotification);
                                     }
                                 });
        };
        clearLoop.setButtonText("Clear loop");
        clearLoop.setTooltip("Discards the captured/imported loop.");
        clearLoop.onClick = [this] { processor.clearLoop(); if (processor.getSourceMode() == 2) { processor.setSourceMode(0); source.setSelectedId(1, juce::dontSendNotification); } };
        setupLabel(loopInfo, "");
        stretchToggle.setButtonText("Stretch to host tempo (keeps pitch)");
        stretchToggle.setTooltip("Off (default): the loop is beat-locked and its pitch follows the tempo. On: a pitch-preserving copy of the loop is rendered in the background whenever the tempo settles (ratio 0.5 to 2.0). Transients can double or smear a little; until the new copy is ready the loop plays at the old tempo's speed.");
        stretchToggle.setClickingTogglesState(true);
        setupLabel(stretchInfo, "");
        setupLabel(message, "");
        message.setColour(juce::Label::textColourId, juce::Colours::orange);

        setupLabel(bandTitle, "Band split (effects on part of the spectrum)");
        setupLabel(bandModeLabel, "Process");
        bandMode.addItemList({"Full band", "Protect lows", "Lows only"}, 1);
        bandMode.setTooltip("Full band: unchanged. Protect lows: bass below the crossover bypasses the effects (keeps the low end solid). Lows only: only the bass goes through the effects. The slope is selectable: 6 dB/oct is gentle, 12 dB/oct is steeper.");
        bandSlope.addItemList({"6 dB/oct", "12 dB/oct"}, 1);
        bandSlope.setTooltip("Slope of the crossover filter around the effected band. 12 dB/oct rejects the other band more strongly; "
                             "the bypassed band is always the exact complement, so nothing is lost when the effects are off.");
        addAndMakeVisible(bandSlope);
        bandTopology.addItemList({"Classic", "Linkwitz-Riley 12", "Linkwitz-Riley 24"}, 1);
        bandTopology.setTooltip("Crossover type. Classic: the original filters (the slope menu applies). Linkwitz-Riley 12 or 24 dB/oct: the low and high parts sum back to a flat level; the slope menu is ignored.");
        addAndMakeVisible(bandTopology);
        setupLabel(crossoverLabel, "Crossover");
        crossover.setSliderStyle(juce::Slider::LinearHorizontal);
        crossover.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
        crossover.setTextValueSuffix(" Hz");
        crossover.setTooltip("Crossover frequency, 40-400 Hz.");

        setupLabel(triggerTitle, "Audio trigger (F07)");
        setupLabel(triggerLabel, "On transient");
        trigger.addItemList({"Off", "Next scene", "Trigger queued"}, 1);
        trigger.setTooltip("Off, switch to the next scene (using the Transition setting) or fire a queued Manual transition whenever a drum-like transient is detected in the signal entering the effects.");
        setupLabel(sensLabel, "Sensitivity");
        sens.setSliderStyle(juce::Slider::LinearHorizontal);
        sens.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
        sens.setNumDecimalPlacesToDisplay(2);
        sens.setTooltip("Higher reacts to softer transients (and false triggers).");
        setupLabel(onsets, "");

        midiMacros.setButtonText("MIDI macros: CC 20/21/22 and program change");
        midiMacros.setTooltip("CC 20 = Intensity, CC 21 = Dry/Wet, CC 22 = Morph amount; program change 0-3 = scene A-D. Applied at about 20 Hz via the host parameters. Off by default; not saved.");
        midiMacros.onClick = [this] { processor.setMidiMacrosEnabled(midiMacros.getToggleState()); };

        for (auto* c : std::initializer_list<juce::Component*>{&sourceTitle, &source, &bars, &loopBpmField, &capture, &importButton, &clearLoop, &matchTempo, &loopInfo, &stretchToggle, &stretchInfo, &waveform,
                                                               &message, &bandTitle, &bandModeLabel, &bandMode, &crossoverLabel, &crossover,
                                                               &triggerTitle, &triggerLabel, &trigger, &sensLabel, &sens, &onsets, &midiMacros})
            addAndMakeVisible(*c);
        auto& apvts = processor.parameters;
        stretchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, GrooveMorphAudioProcessor::loopStretchId, stretchToggle);
        combos.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::bandModeId, bandMode));
        combos.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::bandSlopeId, bandSlope));
        combos.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::bandTopologyId, bandTopology));
        linkParam(bandTopology, GrooveMorphAudioProcessor::bandTopologyId);
        linkParam(bandMode, GrooveMorphAudioProcessor::bandModeId);
        linkParam(bandSlope, GrooveMorphAudioProcessor::bandSlopeId);
        sliders.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::bandCrossoverId, crossover));
        linkParam(crossover, GrooveMorphAudioProcessor::bandCrossoverId);
        combos.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::audioTriggerId, trigger));
        linkParam(trigger, GrooveMorphAudioProcessor::audioTriggerId);
        sliders.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::audioTriggerSensId, sens));
        linkParam(sens, GrooveMorphAudioProcessor::audioTriggerSensId);
        sens.setNumDecimalPlacesToDisplay(2);
    }
    void refresh(const gm::VariationSettings&) override
    {
        const juce::ScopedValueSetter<bool> guard(refreshing, true);
        if (!source.isPopupActive())
            source.setSelectedId(processor.getSourceMode() + 1, juce::dontSendNotification);
        using State = groovemorph::core::LoopCapture::State;
        switch (processor.getLoopCaptureState())
        {
            case State::Idle:
                capture.setButtonText("Capture");
                if (const int why = processor.getLoopCaptureFailure(); why != lastCaptureFailure)
                {
                    lastCaptureFailure = why; // report each failure once, so later messages (e.g. "Imported ...") are not overwritten
                    if (why == 1)
                        message.setText("Capture refused: that many bars would exceed 20 seconds at this tempo.", juce::dontSendNotification);
                    else if (why == 2)
                        message.setText("Capture stopped: the transport stopped or jumped while recording.", juce::dontSendNotification);
                    else if (why == 3)
                        message.setText("Capture stopped: the tempo changed while recording. Keep the tempo steady and try again.", juce::dontSendNotification);
                    else if (why == 4)
                        message.setText("Capture stopped: the meter changed, so the bar length no longer matches.", juce::dontSendNotification);
                    else if (why == 5)
                        message.setText("The capture is kept but could not be loaded yet; retrying.", juce::dontSendNotification);
                }
                break;
            case State::Armed: capture.setButtonText("Waiting for bar line (click to cancel)"); break;
            case State::Recording: capture.setButtonText("Recording... (click to cancel)"); break;
            case State::Done: capture.setButtonText("Finishing..."); break;
        }
        loopInfo.setText(processor.hasLoop() ? "Loop loaded: " + juce::String(processor.getLoopSeconds(), 2) + " s, " + juce::String(processor.getLoopBeats(), 2) + " beats = " + juce::String(processor.getLoopBpm(), 1) + " BPM"
                                             : "No loop loaded (capture or import one).",
                         juce::dontSendNotification);
        clearLoop.setEnabled(processor.hasLoop());
        matchTempo.setEnabled(processor.hasLoop());
        {
            juce::String text;
            if (processor.isLoopStretchEnabled() && processor.hasLoop())
                text = processor.isLoopStretchBusy() ? "Stretching to the host tempo..."
                       : "Stretch ratio " + juce::String(processor.getLoopStretchRatio(), 3)
                             + (processor.isLoopStretchLimited() ? " (limited to 0.5-2.0 or 20 s; the rest is varispeed)" : "");
            switch (processor.getLoopPersistWarning())
            {
                case 1: text += (text.isEmpty() ? "" : "  ") + juce::String("Loop too large to save with the project."); break;
                case 2: text += (text.isEmpty() ? "" : "  ") + juce::String("Loop could not be saved with the project."); break;
                case 3: text += (text.isEmpty() ? "" : "  ") + juce::String("The loop saved in this project could not be restored."); break;
                default: break;
            }
            stretchInfo.setText(text, juce::dontSendNotification);
        }
        {
            std::vector<float> lo, hi;
            processor.getLoopPeaks(waveform.preferredColumns(), lo, hi);
            waveform.setPeaks(lo, hi);
            const auto grid = processor.getGridContext();
            waveform.setGrid(processor.hasLoop() ? processor.getLoopBeats() : 0.0, processor.getLoopOriginBeats(), grid.beatsPerBar);
            waveform.setPlayhead(processor.getLoopPlayFraction());
        }
        crossover.setEnabled(bandMode.getSelectedId() > 1); // the crossover only matters when the bass is split
        sens.setEnabled(trigger.getSelectedId() > 1);        // sensitivity only matters when a trigger action is chosen
        onsets.setText("Transients detected: " + juce::String(processor.getOnsetCount()), juce::dontSendNotification);
        midiMacros.setToggleState(processor.areMidiMacrosEnabled(), juce::dontSendNotification);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        sourceTitle.setBounds(nextRow(area));
        source.setBounds(nextRow(area).removeFromLeft(240));
        auto r = nextRow(area);
        bars.setBounds(r.removeFromLeft(100));
        loopBpmField.setBounds(r.removeFromLeft(64).reduced(4, 0));
        capture.setBounds(r.removeFromLeft(180).reduced(4, 0));
        r = nextRow(area);
        importButton.setBounds(r.removeFromLeft(120));
        clearLoop.setBounds(r.removeFromLeft(100).reduced(4, 0));
        matchTempo.setBounds(r.removeFromLeft(150).reduced(4, 0));
        loopInfo.setBounds(nextRow(area));
        r = nextRow(area);
        stretchToggle.setBounds(r.removeFromLeft(250));
        stretchInfo.setBounds(r);
        waveform.setBounds(nextRow(area, 44));
        message.setBounds(nextRow(area));
        area.removeFromTop(8);
        bandTitle.setBounds(nextRow(area));
        r = nextRow(area);
        bandModeLabel.setBounds(r.removeFromLeft(100));
        bandMode.setBounds(r.removeFromLeft(160));
        bandSlope.setBounds(r.removeFromLeft(110).reduced(6, 0));
        r = nextRow(area);
        crossoverLabel.setBounds(r.removeFromLeft(100));
        bandTopology.setBounds(r.removeFromRight(170).reduced(6, 0));
        crossover.setBounds(r);
        area.removeFromTop(8);
        triggerTitle.setBounds(nextRow(area));
        r = nextRow(area);
        triggerLabel.setBounds(r.removeFromLeft(100));
        trigger.setBounds(r.removeFromLeft(180));
        r = nextRow(area);
        sensLabel.setBounds(r.removeFromLeft(100));
        sens.setBounds(r);
        onsets.setBounds(nextRow(area));
        area.removeFromTop(8);
        midiMacros.setBounds(nextRow(area));
    }
private:
    // 0 = automatic (import); a capture needs a number, so Auto records 2 bars.
    int barChoice() const { constexpr int values[] = {0, 1, 2, 4, 8}; return values[juce::jlimit(0, 4, bars.getSelectedId() - 1)]; }
    WaveformView waveform;
    MenuToggleButton stretchToggle;
    juce::Label stretchInfo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stretchAttachment;
    juce::Label sourceTitle, loopInfo, message, bandTitle, bandModeLabel, crossoverLabel, triggerTitle, triggerLabel, sensLabel, onsets;
    MenuComboBox source, bars, bandMode, bandSlope, bandTopology, trigger;
    int lastCaptureFailure = 0;
    juce::TextButton capture, importButton, clearLoop, matchTempo;
    juce::TextEditor loopBpmField;
    MenuSlider crossover, sens;
    MenuToggleButton midiMacros;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<std::unique_ptr<SliderAttachment>> sliders;
    std::vector<std::unique_ptr<ComboAttachment>> combos;
};

// ---------------------------------------------------------------------------------------
// Perform tab: live macros (F04), gesture lane (F11), phrase evolution (F05)
// ---------------------------------------------------------------------------------------
class PerformTab final : public InspectorTab
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    PerformTab(GrooveMorphAudioProcessor& p, SceneEditAccess a) : InspectorTab(p, a)
    {
        for (std::size_t i = 0; i < sceneButtons.size(); ++i)
        {
            sceneButtons[i].setButtonText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)));
            sceneButtons[i].setTooltip("Launch this scene using the Transition setting below.");
            sceneButtons[i].onClick = [this, i] { processor.requestVariation(i); };
            addAndMakeVisible(sceneButtons[i]);
        }
        setupLabel(transitionLabel, "Transition");
        transition.addItemList({"Immediate", "Next Beat", "Next Bar", "Next Phrase", "Manual"}, 1);
        transition.setTooltip("When a scene launch takes effect.");
        trigger.setButtonText("Trigger queued");
        trigger.setTooltip("Fires a queued Manual launch now.");
        trigger.onClick = [this] { processor.triggerManualSwitch(); };
        cancel.setButtonText("Cancel queued");
        cancel.setTooltip("Cancels a queued launch.");
        cancel.onClick = [this] { processor.cancelQueuedSwitch(); };
        const char* names[] = {"Intensity", "Dry / wet", "Morph amount", "Chaos"};
        for (std::size_t i = 0; i < macros.size(); ++i)
        {
            setupLabel(macroLabels[i], names[i]);
            macros[i].setSliderStyle(juce::Slider::LinearHorizontal);
            macros[i].setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
            macros[i].setNumDecimalPlacesToDisplay(2);
            addAndMakeVisible(macroLabels[i]);
            addAndMakeVisible(macros[i]);
        }
        setupLabel(morphLabel, "Morph toward");
        morphTarget.addItemList({"Off", "A", "B", "C", "D"}, 1);

        setupLabel(gestureTitle, "Gesture lane (records one phrase of movement)");
        gestureTarget.addItemList({"Morph amount", "Intensity", "Dry / wet"}, 1);
        gestureTarget.setSelectedId(1, juce::dontSendNotification);
        gestureTarget.setTooltip("Which macro the gesture lane records and plays back.");
        gestureTarget.onChange = [this] { if (!refreshing) processor.setGestureTarget(juce::jmax(0, gestureTarget.getSelectedId() - 1)); };
        record.setButtonText("Record");
        record.setClickingTogglesState(true);
        record.setTooltip("Changing the target clears the lane. While the transport plays, records the target's value by phrase position (move the slider while recording). Overwrites as the phrase loops.");
        record.onClick = [this] { processor.setGestureRecording(record.getToggleState()); };
        play.setButtonText("Play");
        play.setClickingTogglesState(true);
        play.setTooltip("Plays the recorded gesture back by phrase position (about 20 updates per second).");
        play.onClick = [this] { processor.setGesturePlaying(play.getToggleState()); if (play.getToggleState() && !processor.isGesturePlaying()) play.setToggleState(false, juce::dontSendNotification); };
        clearGesture.setButtonText("Clear");
        clearGesture.onClick = [this] { processor.clearGesture(); };
        setupLabel(gestureInfo, "");

        setupLabel(evolveTitle, "Phrase evolution (sounding only, never edits scenes)");
        evolveOn.setButtonText("Evolve scene");
        evolveOn.setTooltip("Every N phrases the chosen scene is replaced, for sounding only, by a deterministic variation of its stored settings. Stored scenes and project state are never changed. Locks are respected.");
        evolveOn.onClick = [this] { applyEvolution(); };
        evolveScene.addItemList({"A", "B", "C", "D"}, 1);
        evolveScene.setSelectedId(1, juce::dontSendNotification);
        evolveScene.onChange = [this] { applyEvolution(); };
        evolveStyle.addItemList({"Conservative", "Creative", "Experimental"}, 1);
        evolveStyle.setSelectedId(1, juce::dontSendNotification);
        evolveStyle.onChange = [this] { applyEvolution(); };
        every.setup(*this, "Every N phrases", "How often the evolving scene changes.", 1, 16, 1, 0);
        every.slider.setValue(2, juce::dontSendNotification);
        every.commit = [this] { applyEvolution(); };
        distance.setup(*this, "Distance", "How far each step moves from the stored scene.", 0, 1, 0.01, 2);
        distance.slider.setValue(0.3, juce::dontSendNotification);
        distance.commit = [this] { applyEvolution(); };
        setupLabel(evolveInfo, "");

        setupLabel(phaseTitle, "Scene start phase (edited scene): where its pattern starts when it launches");
        phasePolicy.addItemList({"Follow the phrase", "Restart at launch", "Snap to next beat"}, 1);
        phasePolicy.setTooltip("Preserve: the pattern keeps running with the phrase. Restart: the pattern starts from its "
                               "beginning at launch. Snap: the pattern jumps forward to the next meter beat. The launch "
                               "time still follows the Transition setting.");
        phasePolicy.onChange = [this] {
            edit([this](auto& s) { s.control.phase.policy = static_cast<gm::control::PhasePolicy>(juce::jmax(0, phasePolicy.getSelectedId() - 1)); });
        };
        phaseOffset.setup(*this, "Phase offset (beats)", "Added to the pattern start, -4 to +4 meter beats (step 1/16).", -4.0, 4.0, 1.0 / 16.0, 3);
        phaseOffset.commit = [this] {
            edit([this](auto& s) { s.control.phase.offsetBeats = static_cast<float>(phaseOffset.slider.getValue()); });
        };
        phaseSmooth.setup(*this, "Transition smoothing (ms)", "Length of the cross-fade when the pattern jumps as this scene launches. 0 = default (3 ms), up to 250 ms.", 0.0, 250.0, 1.0, 0);
        phaseSmooth.commit = [this] {
            edit([this](auto& s) { s.control.phase.smoothingMs = static_cast<float>(phaseSmooth.slider.getValue()); });
        };
        restartScene.setButtonText("Restart scene now");
        restartScene.setTooltip("Restarts the active scene's pattern now. The transport and the active scene do not change.");
        restartScene.onClick = [this] { processor.restartActiveScene(); };
        setupLabel(phaseInfo, "");
        for (auto* c : std::initializer_list<juce::Component*>{&phaseTitle, &phasePolicy, &restartScene, &phaseInfo})
            addAndMakeVisible(*c);

        for (auto* c : std::initializer_list<juce::Component*>{&transitionLabel, &transition, &trigger, &cancel, &morphLabel, &morphTarget,
                                                               &gestureTitle, &gestureTarget, &record, &play, &clearGesture, &gestureInfo,
                                                               &evolveTitle, &evolveOn, &evolveScene, &evolveStyle, &evolveInfo})
            addAndMakeVisible(*c);
        auto& apvts = processor.parameters;
        combos.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::transitionModeId, transition));
        linkParam(transition, GrooveMorphAudioProcessor::transitionModeId);
        sliders.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::masterIntensityId, macros[0]));
        linkParam(macros[0], GrooveMorphAudioProcessor::masterIntensityId);
        sliders.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::dryWetId, macros[1]));
        linkParam(macros[1], GrooveMorphAudioProcessor::dryWetId);
        sliders.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::morphAmountId, macros[2]));
        linkParam(macros[2], GrooveMorphAudioProcessor::morphAmountId);
        sliders.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::chaosId, macros[3]));
        linkParam(macros[3], GrooveMorphAudioProcessor::chaosId);
        combos.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::morphTargetId, morphTarget));
        linkParam(morphTarget, GrooveMorphAudioProcessor::morphTargetId);
        for (auto& m : macros)
            m.setNumDecimalPlacesToDisplay(2);
        macros[0].setTooltip("Same control as the main Intensity slider: 0 = unprocessed sound, 1 = full effect. With MIDI macros on, CC 20 moves it.");
        macros[1].setTooltip("Same control as the main Dry/wet slider: mix between unprocessed (0) and processed (1) sound. With MIDI macros on, CC 21 moves it.");
        macros[2].setTooltip("How far the sound morphs from the sounding scene towards the Morph target scene (0 = not at all, 1 = fully the target). With MIDI macros on, CC 22 moves it.");
        macros[3].setTooltip("Same control as Chaos in Easy mode: makes the sounding scene busier (more effects, deeper and shorter slices) without changing the stored scenes. 0 = the scene exactly as stored.");
        morphTarget.setTooltip("Scene to morph toward. Off disables morphing. Same control as in the Morph tab.");
        transition.setTooltip("When a scene change takes effect: Immediate, next beat, next bar, next phrase, or Manual (waits for Trigger). Same control as the main window.");
        evolveScene.setTooltip("Scene that evolves while it sounds: every N phrases it plays as a slightly changed variation of its stored settings. The stored scene itself is never edited.");
        evolveStyle.setTooltip("How bold each evolution step is: Conservative (small changes), Creative, or Experimental (large changes).");
        clearGesture.setTooltip("Erases the recorded gesture so a new one can be recorded.");
        gestureTarget.setTooltip("Which control the gesture records and plays back: Morph amount, Intensity or Dry/wet. Changing it erases the recorded gesture.");
    }
    void refresh(const gm::VariationSettings& settings) override
    {
        const juce::ScopedValueSetter<bool> guard(refreshing, true);
        const auto active = processor.getActiveVariation();
        const auto queued = processor.getQueuedVariation();
        if (!phasePolicy.isPopupActive())
            phasePolicy.setSelectedId(static_cast<int>(settings.control.phase.policy) + 1, juce::dontSendNotification);
        phaseOffset.set(settings.control.phase.offsetBeats);
        phaseSmooth.set(settings.control.phase.smoothingMs);
        const double shift = processor.getPhaseShiftBeats();
        phaseInfo.setText(shift == 0.0 ? "Pattern follows the transport."
                                       : "Pattern is shifted by " + juce::String(shift, 3) + " beats from the transport.",
                          juce::dontSendNotification);
        for (std::size_t i = 0; i < sceneButtons.size(); ++i)
            sceneButtons[i].setColour(juce::TextButton::buttonColourId,
                                      i == active ? juce::Colour(0xff2e7d32)
                                                  : static_cast<int>(i) == queued ? juce::Colour(0xff8d6e00) : juce::Colour(0xff3a4658));
        trigger.setEnabled(queued >= 0);
        cancel.setEnabled(queued >= 0);
        record.setToggleState(processor.isGestureRecording(), juce::dontSendNotification);
        play.setToggleState(processor.isGesturePlaying(), juce::dontSendNotification);
        play.setEnabled(processor.getGestureFill() > 0);
        gestureInfo.setText(processor.getGestureFill() > 0 ? "Recorded bins: " + juce::String(processor.getGestureFill()) + " / 256"
                                                           : "Nothing recorded.", juce::dontSendNotification);
        if (!evolveScene.isPopupActive() && !evolveStyle.isPopupActive() && !every.slider.isMouseButtonDown()
            && !distance.slider.isMouseButtonDown())
        {
            const auto evo = processor.getEvolutionSettings();
            if (evo.enabled || !evolveSynced || evolveOn.getToggleState()) // also follows a Reset that turned it off
            {
                evolveOn.setToggleState(evo.enabled, juce::dontSendNotification);
                if (evo.enabled)
                {
                    evolveScene.setSelectedId(static_cast<int>(evo.scene) + 1, juce::dontSendNotification);
                    evolveStyle.setSelectedId(evo.style + 1, juce::dontSendNotification);
                    every.slider.setValue(evo.everyPhrases, juce::dontSendNotification);
                    distance.slider.setValue(evo.distance, juce::dontSendNotification);
                }
                evolveSynced = true;
            }
        }
        if (!gestureTarget.isPopupActive())
            gestureTarget.setSelectedId(processor.getGestureTarget() + 1, juce::dontSendNotification);
        evolveInfo.setText(processor.isEvolving() ? "Evolving: step " + juce::String(processor.getEvolutionStep()) : "Evolution off.",
                           juce::dontSendNotification);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto r = nextRow(area, 44);
        for (auto& b : sceneButtons)
            b.setBounds(r.removeFromLeft(r.getWidth() / (4 - static_cast<int>(&b - sceneButtons.data()))).reduced(3, 0));
        r = nextRow(area);
        transitionLabel.setBounds(r.removeFromLeft(90));
        transition.setBounds(r.removeFromLeft(130));
        trigger.setBounds(r.removeFromLeft(120).reduced(4, 0));
        cancel.setBounds(r.removeFromLeft(120).reduced(4, 0));
        for (std::size_t i = 0; i < macros.size(); ++i)
        {
            r = nextRow(area);
            macroLabels[i].setBounds(r.removeFromLeft(110));
            macros[i].setBounds(r);
        }
        r = nextRow(area);
        morphLabel.setBounds(r.removeFromLeft(110));
        morphTarget.setBounds(r.removeFromLeft(110));
        area.removeFromTop(8);
        gestureTitle.setBounds(nextRow(area));
        r = nextRow(area);
        gestureTarget.setBounds(r.removeFromLeft(140));
        record.setBounds(r.removeFromLeft(80).reduced(4, 0));
        play.setBounds(r.removeFromLeft(80).reduced(4, 0));
        clearGesture.setBounds(r.removeFromLeft(70).reduced(4, 0));
        gestureInfo.setBounds(nextRow(area));
        area.removeFromTop(8);
        evolveTitle.setBounds(nextRow(area));
        r = nextRow(area);
        evolveOn.setBounds(r.removeFromLeft(130));
        evolveScene.setBounds(r.removeFromLeft(60));
        evolveStyle.setBounds(r.removeFromLeft(140).reduced(4, 0));
        every.layout(nextRow(area), 120);
        distance.layout(nextRow(area), 120);
        evolveInfo.setBounds(nextRow(area));
        area.removeFromTop(8);
        phaseTitle.setBounds(nextRow(area));
        r = nextRow(area);
        phasePolicy.setBounds(r.removeFromLeft(170));
        restartScene.setBounds(r.removeFromLeft(150).reduced(4, 0));
        phaseOffset.layout(nextRow(area), 150);
        phaseSmooth.layout(nextRow(area), 150);
        phaseInfo.setBounds(nextRow(area));
    }
private:
    void applyEvolution()
    {
        if (refreshing)
            return;
        processor.setEvolution(evolveOn.getToggleState(), static_cast<std::size_t>(juce::jmax(0, evolveScene.getSelectedId() - 1)),
                               static_cast<int>(every.slider.getValue()), static_cast<float>(distance.slider.getValue()),
                               evolveStyle.getSelectedId() - 1, 0x47524F4F56ull);
    }
    bool evolveSynced = false;
    std::array<juce::TextButton, 4> sceneButtons;
    juce::Label transitionLabel, morphLabel, gestureTitle, gestureInfo, evolveTitle, evolveInfo, phaseTitle, phaseInfo;
    juce::ComboBox phasePolicy;
    juce::TextButton restartScene;
    SliderRow phaseOffset, phaseSmooth;
    std::array<juce::Label, 4> macroLabels;
    std::array<MenuSlider, 4> macros;
    MenuComboBox transition, morphTarget, gestureTarget, evolveScene, evolveStyle;
    juce::TextButton trigger, cancel, record, play, clearGesture;
    MenuToggleButton evolveOn;
    SliderRow every, distance;
    std::vector<std::unique_ptr<SliderAttachment>> sliders;
    std::vector<std::unique_ptr<ComboAttachment>> combos;
};

// ---------------------------------------------------------------------------------------
// Status tab: honest feature list
// ---------------------------------------------------------------------------------------
class StatusTab final : public juce::Component
{
public:
    StatusTab()
    {
        text.setMultiLine(true);
        text.setReadOnly(true);
        text.setCaretVisible(false);
        text.setTooltip("Honest list of what this alpha implements and what it does not (read only).");
        text.setText(
            "ALPHA FEATURE STATUS (see docs/alpha/ALPHA-FEATURE-STATUS.md)\n\n"
            "Implemented (automated tests; not tried in a DAW or by ear):\n"
            " - Placement window/cadence/origin/snap, downbeat protect, up to 4 protected regions, event lanes\n"
            "   with per-event depth, slice, priority, cycle restart and lock; per-lane loop lengths\n"
            " - Generator: seed, style, distance, three-option gallery, event-pattern generation, locks,\n"
            "   preview/apply/revert, locally saved 24-entry history\n"
            " - Scenes: duplicate, reset, copy FX, rename, undo/redo, morph, auto-return (bars/phrases),\n"
            "   scene start phase (follow / restart / snap to next beat, offset, Restart scene now)\n"
            " - Limits: effects at once + priority; spacing/coverage via Reduce; Events x2 and /2\n"
            " - Input: demo loop, capture/import loop (beat-locked), band split (6 or 12 dB/oct), audio\n"
            "   trigger, MIDI CC/program-change macros (tick MIDI macros first)\n"
            " - Perform: scene launch, macros, gesture lane, phrase evolution\n"
            " - Print: record the processed output for N bars to a 24-bit WAV, drag it into the DAW (untested)\n"
            " - Right-click on any control: exact value, copy/paste, reset, MIDI learn; Visual window\n"
            " - Host Variation parameter follows the sounding scene\n\n"
            "Not implemented / limited in this alpha:\n"
            " - Per-scene transition smoothing time; Linkwitz-Riley or steeper crossovers; loop-aware Reduce\n"
            " - No time stretching for loops; loop audio is not saved with the project. Generator history\n"
            "   is saved locally on this computer, not embedded in the DAW project\n"
            "   (gesture curve, evolution settings and MIDI links are); gestures run at ~20 Hz\n"
            " - Capture is stereo-limited, 20 s max; CLAP and other formats\n\n"
            "Untested: real DAW hosts, audio hardware, listening quality.",
            false);
        addAndMakeVisible(text);
    }
    void resized() override { text.setBounds(getLocalBounds().reduced(6)); }
private:
    juce::TextEditor text;
};
} // namespace

struct InspectorPanel::Impl
{
    Impl(GrooveMorphAudioProcessor& p, SceneEditAccess a)
        : access(a),
          placement(std::make_unique<PlacementTab>(p, a)),
          generator(std::make_unique<GeneratorTab>(p, a)),
          scenes(std::make_unique<ScenesTab>(p, a)),
          morph(std::make_unique<MorphTab>(p, a)),
          limits(std::make_unique<LimitsTab>(p, a)),
          input(std::make_unique<InputTab>(p, a)),
          perform(std::make_unique<PerformTab>(p, a)),
          status(std::make_unique<StatusTab>()),
          print(std::make_unique<PrintPanel>(p)),
          tabs(juce::TabbedButtonBar::TabsAtTop)
    {
        const auto bg = juce::Colour(0xff1b232e);
        // Short names keep eight tabs on one row; the tooltips carry the full names.
        const char* names[] = {"Place", "Gen", "Scene", "Morph", "Limit", "Input", "Perform", "Info", "Print"};
        const char* full[] = {"Place: choose where in the phrase effects act (window, every N phrases) and draw event lanes",
                              "Gen: generate variations of a scene (seed, style, distance) and lock parts of it",
                              "Scene: duplicate, copy, reset and clear scenes and effects",
                              "Morph: blend two scenes, and automatically return to a scene after a few bars or phrases",
                              "Limit: cap how many effects play at once and how busy a scene may get",
                              "Input: choose the input source, capture or import a loop, split the bass, audio triggers, MIDI macros",
                              "Perform: live macros, record and play back a gesture, evolve a scene over phrases",
                              "Info: what this alpha version implements and what is untested",
                              "Print: record the processed output for N bars to a WAV file, drag it into your DAW or reuse it as the loop source"};
        juce::Component* contents[] = {placement.get(), generator.get(), scenes.get(), morph.get(), limits.get(), input.get(),
                                       perform.get(), status.get(), print.get()};
        for (int i = 0; i < 9; ++i)
        {
            tabs.addTab(names[i], bg, contents[i], false);
            if (auto* button = tabs.getTabbedButtonBar().getTabButton(i))
                button->setTooltip(full[i]);
        }
    }
    SceneEditAccess access;
    std::unique_ptr<PlacementTab> placement;
    std::unique_ptr<GeneratorTab> generator;
    std::unique_ptr<ScenesTab> scenes;
    std::unique_ptr<MorphTab> morph;
    std::unique_ptr<LimitsTab> limits;
    std::unique_ptr<InputTab> input;
    std::unique_ptr<PerformTab> perform;
    std::unique_ptr<StatusTab> status;
    std::unique_ptr<PrintPanel> print; // N04
    juce::TabbedComponent tabs;
};

InspectorPanel::InspectorPanel(GrooveMorphAudioProcessor& processor, SceneEditAccess access)
    : impl(std::make_unique<Impl>(processor, std::move(access)))
{
    addAndMakeVisible(impl->tabs);
    finishControlMenus(*this, processor);
    // Only the current tab is a child of the tab component; name the controls of every tab.
    for (int i = 0; i < impl->tabs.getNumTabs(); ++i)
        if (auto* content = impl->tabs.getTabContentComponent(i))
            finishControlMenus(*content, processor);
}

InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::resized() { impl->tabs.setBounds(getLocalBounds()); }

void InspectorPanel::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff1b232e)); }

void InspectorPanel::showTab(int index) { impl->tabs.setCurrentTabIndex(index); }

void InspectorPanel::refresh()
{
    const auto settings = impl->access.settings();
    impl->placement->refresh(settings);
    impl->generator->refresh(settings);
    impl->scenes->refresh(settings);
    impl->morph->refresh(settings);
    impl->limits->refresh(settings);
    impl->input->refresh(settings);
    impl->perform->refresh(settings);
}
} // namespace groovemorph::ui
