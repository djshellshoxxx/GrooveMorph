#include "ui/InspectorPanel.h"

#include "PluginProcessor.h"
#include "core/control/PlacementEvaluator.h"
#include "core/control/SceneControl.h"
#include "core/variation/CandidateBuilder.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace gm = groovemorph::core;
namespace ctl = groovemorph::core::control;
namespace gvar = groovemorph::core::variation;

namespace groovemorph::ui
{
namespace
{
const juce::StringArray operationNames{"Repeat / stutter", "Reverse slice", "Gate", "Half-time", "Tape stop"};
const juce::StringArray operationShort{"Rpt", "Rev", "Gate", "Half", "Stop"};
const juce::Colour operationColours[] = {juce::Colour(0xff4fc3f7), juce::Colour(0xffba68c8),
                                         juce::Colour(0xff81c784), juce::Colour(0xffffb74d),
                                         juce::Colour(0xffe57373)};
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
    juce::Slider slider;
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
                   "nudge by one grid step (Shift: one beat). Events snap to the Snap grid.");
    }

    std::function<void()> onSelectionChanged;
    std::function<void(const juce::String&)> onMessage;

    int selected() const noexcept { return selectedIndex; }
    void setSettingsView(const gm::VariationSettings& s)
    {
        if (!dragging)
            draft = s.control;
        view = s;
        repaint();
    }
    bool deleteSelected()
    {
        if (selectedIndex < 0)
            return false;
        auto next = access.settings().control;
        if (isLocked(next.lane.events[static_cast<std::size_t>(selectedIndex)].operation))
            return false;
        ctl::removeEvent(next.lane, static_cast<std::size_t>(selectedIndex));
        commit(next);
        selectedIndex = -1;
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
                    g.setColour(operationColours[e.operation % 5].withAlpha(e.enabled ? 0.35f + 0.55f * e.depth : 0.15f));
                    g.fillRoundedRectangle(rect, 3.0f);
                    g.setColour(static_cast<int>(i) == selectedIndex ? juce::Colours::white
                                                                     : operationColours[e.operation % 5]);
                    g.drawRoundedRectangle(rect, 3.0f, static_cast<int>(i) == selectedIndex ? 2.0f : 1.0f);
                }
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
        if (hasKeyboardFocus(false))
        {
            g.setColour(juce::Colours::white.withAlpha(0.4f));
            g.drawRect(getLocalBounds(), 1);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();
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
        selectedIndex = hit(control, lane, tick, P);
        dragging = true;
        dragMoved = false;
        if (selectedIndex >= 0)
        {
            const auto& e = control.lane.events[static_cast<std::size_t>(selectedIndex)];
            if (isLocked(e.operation))
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
            if (std::abs(tick - e.onsetTicks) <= edge)
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
                    onMessage(status == ctl::EditStatus::Capacity ? "Event limit reached (32)."
                                                                  : "Events of one operation cannot overlap.");
                return;
            }
            selectedIndex = draft.lane.count - 1;
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
            onset = std::clamp<std::int64_t>(onset, 0, pt - original.durationTicks);
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
        if (!draft.lane.explicitEvents)
            return;
        const auto grid = processor.getGridContext();
        const int lane = (event.y - laneTop) / laneHeight;
        if (lane < 0 || lane >= laneCount)
            return;
        auto control = access.settings().control;
        const int index = hit(control, lane, tickAt(event.x, grid), static_cast<double>(grid.phraseTicks()));
        if (index >= 0)
        {
            selectedIndex = index;
            deleteSelected();
        }
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (selectedIndex < 0)
            return false;
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
            return deleteSelected();
        const bool left = key.getKeyCode() == juce::KeyPress::leftKey;
        const bool right = key.getKeyCode() == juce::KeyPress::rightKey;
        if (!left && !right)
            return false;
        auto control = access.settings().control;
        const auto grid = processor.getGridContext();
        auto e = control.lane.events[static_cast<std::size_t>(selectedIndex)];
        if (isLocked(e.operation))
            return true;
        const std::int64_t step = key.getModifiers().isShiftDown() ? ctl::ticksPerBeat : std::max<std::int64_t>(1, unit(control));
        auto onset = static_cast<std::int64_t>(e.onsetTicks) + (right ? step : -step);
        onset = std::clamp<std::int64_t>(onset, 0, grid.phraseTicks() - e.durationTicks);
        e.onsetTicks = static_cast<std::int32_t>(onset);
        if (ctl::replaceEvent(control.lane, static_cast<std::size_t>(selectedIndex), e, grid.phraseTicks()) == ctl::EditStatus::Ok)
            commit(control);
        return true;
    }

    void select(int index)
    {
        selectedIndex = index;
        repaint();
    }

private:
    enum class Mode { Move, ResizeStart, ResizeEnd };
    static constexpr int laneCount = 5;
    static constexpr int laneHeight = 24;
    static constexpr int laneTop = 18;
    static constexpr int labelWidth = 72;

    GrooveMorphAudioProcessor& processor;
    SceneEditAccess access;
    gm::VariationSettings view;
    ctl::SceneControl draft;
    int selectedIndex = -1;
    int dragLane = 0;
    bool dragging = false, dragMoved = false;
    bool snapEnabled = true;
    Mode mode = Mode::Move;
    std::int64_t grabOffset = 0;
    ctl::LaneEvent original;

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
    static int hit(const ctl::SceneControl& c, int lane, std::int64_t tick, double P) noexcept
    {
        for (std::size_t i = 0; i < c.lane.count; ++i)
        {
            const auto& e = c.lane.events[i];
            if (e.operation != lane)
                continue;
            double d = std::fmod(static_cast<double>(tick - e.onsetTicks), P);
            if (d < 0.0)
                d += P;
            if (d < static_cast<double>(e.durationTicks) || std::abs(static_cast<double>(tick - e.onsetTicks)) < 1.0)
                return static_cast<int>(i);
        }
        return -1;
    }
    // Pixel ranges of one event; wrapping events split in two.
    static std::vector<std::pair<int, int>> eventRanges(const ctl::LaneEvent& e, double P, int width)
    {
        std::vector<std::pair<int, int>> ranges;
        const double a = e.onsetTicks, b = a + e.durationTicks;
        if (b <= P)
            ranges.emplace_back(static_cast<int>(a / P * width), static_cast<int>(b / P * width));
        else
        {
            ranges.emplace_back(static_cast<int>(a / P * width), width);
            ranges.emplace_back(0, static_cast<int>((b - P) / P * width));
        }
        return ranges;
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
    PlacementTab(GrooveMorphAudioProcessor& p, SceneEditAccess a) : InspectorTab(p, a), canvas(p, a)
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
                ctl::convertLegacyToEvents(s.control.lane, mask, pt);
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
                                                               &removeSelected, &eventEnabled, &message, &canvas})
            addAndMakeVisible(*c);
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
        start.slider.setEnabled(custom);
        span.slider.setEnabled(custom);
        every.slider.setEnabled(p.enabled);
        offset.slider.setEnabled(p.enabled);
        window.setEnabled(p.enabled);
        for (auto* c : std::initializer_list<juce::Component*>{&enable, &window, &start.slider, &span.slider, &every.slider,
                                                               &offset.slider, &origin.slider, &protect.slider})
            if (locked)
                c->setEnabled(false);
        if (locked)
            message.setText("Placement is locked (Generator tab > Locks).", juce::dontSendNotification);
        convert.setEnabled(!s.control.lane.explicitEvents);
        const int sel = canvas.selected();
        const bool haveSel = s.control.lane.explicitEvents && sel >= 0 && static_cast<std::size_t>(sel) < s.control.lane.count;
        removeSelected.setEnabled(haveSel);
        eventDepth.slider.setEnabled(haveSel);
        eventEnabled.setEnabled(haveSel);
        if (haveSel)
        {
            const auto& e = s.control.lane.events[static_cast<std::size_t>(sel)];
            eventDepth.set(e.depth);
            eventEnabled.setToggleState(e.enabled, juce::dontSendNotification);
        }
        else if (sel >= 0)
            canvas.select(-1);
        canvas.setSettingsView(s);
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
        auto buttons = nextRow(area);
        modeButton.setBounds(buttons.removeFromLeft(130));
        convert.setBounds(buttons.removeFromLeft(130).reduced(2, 0));
        removeSelected.setBounds(buttons.removeFromLeft(100).reduced(2, 0));
        canvas.setBounds(nextRow(area, 18 + 5 * 24 + 6));
        eventDepth.layout(nextRow(area), 150);
        eventEnabled.setBounds(nextRow(area));
        message.setBounds(nextRow(area));
    }

private:
    juce::ToggleButton enable, modeButton, eventEnabled;
    juce::ComboBox window, snap;
    juce::Label snapLabel, message;
    SliderRow start, span, every, offset, origin, protect, eventDepth;
    juce::TextButton convert, removeSelected;
    LaneCanvas canvas;
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
            settings.seed = juce::Random::getSystemRandom().nextInt64();
            settings.candidate = 0;
            seedEditor.setText(juce::String(static_cast<juce::int64>(settings.seed)), false);
            updateCandidateLabel();
        };
        distance.setup(*this, "Distance", "How far the candidate moves from the stored scene: 0 = unchanged, 1 = full generated change. Enable flips need >= 0.5.", 0, 1, 0.01, 2);
        distance.slider.setValue(1.0, juce::dontSendNotification);
        distance.commit = [this] { settings.distance = static_cast<float>(distance.slider.getValue()); };
        generate.setButtonText("Generate preview");
        generate.setTooltip("Builds a candidate from the STORED scene and auditions it. Nothing is committed: "
                            "use 'Apply preview' to keep it or 'Revert preview' to discard it. Replaces unapplied preview edits.");
        generate.onClick = [this] { run(); };
        previous.setButtonText("<");
        previous.setTooltip("Previous candidate number for this seed.");
        previous.onClick = [this] { if (settings.candidate > 0) { --settings.candidate; run(); } };
        regenerate.setButtonText("Regenerate >");
        regenerate.setTooltip("Next candidate for the same seed and source scene.");
        regenerate.onClick = [this] { ++settings.candidate; run(); };
        setupLabel(candidateLabel, "");
        setupLabel(resultLabel, "No candidate generated yet.");
        history.setTextWhenNothingSelected("Generation history");
        history.setTooltip("Previous candidates of this session. Selecting one regenerates it exactly "
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
        setupLabel(locksTitle, "Locks (protect from generation)", "Locked fields are restored after generation. They do not change what you hear.");
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
        for (auto* c : std::initializer_list<juce::Component*>{&style, &styleLabel, &seedLabel, &seedEditor, &newSeed, &generate, &previous,
                                                               &regenerate, &candidateLabel, &resultLabel, &history, &protectLabel,
                                                               &locksTitle, &lockPlacement})
            addAndMakeVisible(*c);
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
            b.setBounds(r.removeFromLeft(r.getWidth() / (5 - static_cast<int>(&b - protectOp.data()))));
        r = nextRow(area, 30);
        generate.setBounds(r.removeFromLeft(150));
        previous.setBounds(r.removeFromLeft(36).reduced(2, 0));
        regenerate.setBounds(r.removeFromLeft(110).reduced(2, 0));
        candidateLabel.setBounds(r);
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
    juce::ComboBox style, history;
    juce::Label styleLabel, seedLabel, protectLabel, locksTitle, candidateLabel, resultLabel;
    juce::TextEditor seedEditor;
    juce::TextButton newSeed, generate, previous, regenerate;
    SliderRow distance;
    std::array<juce::ToggleButton, gm::operationCount> protectOp;
    std::array<juce::Label, gm::operationCount> lockRowLabel;
    std::array<std::array<juce::ToggleButton, 4>, gm::operationCount> locks;
    juce::ToggleButton lockPlacement;

    void readSeed()
    {
        const auto text = seedEditor.getText().trim();
        settings.seed = text.isEmpty() ? 1ull : static_cast<std::uint64_t>(text.getLargeIntValue());
        if (text.length() > 18)
        {
            // getLargeIntValue saturates at int64; parse unsigned 64-bit manually.
            std::uint64_t v = 0;
            for (auto ch : text)
                v = v * 10u + static_cast<std::uint64_t>(ch - '0');
            settings.seed = v;
        }
        settings.candidate = 0;
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
        juce::String text = changed.isEmpty() ? "Candidate changes nothing (try Regenerate, a bolder style or more distance)."
                                              : "Changed: " + changed.joinIntoString(", ");
        if (c.permissionRestores > 0)
            text << "  |  locks restored " << juce::String(c.permissionRestores) << " field(s)";
        text << "  |  Previewing: use Apply preview / Revert preview.";
        resultLabel.setText(text, juce::dontSendNotification);
    }
    void run()
    {
        readSeedQuiet();
        const auto source = access.storedSettings();
        const auto candidate = gvar::buildCandidate(source, settings);
        entries.push_front({source, settings, access.editingIndex()});
        if (entries.size() > 24)
            entries.pop_back();
        rebuildHistory();
        describe(candidate);
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
                                + "  seed " + juce::String(static_cast<juce::int64>(e.settings.seed)) + "  cand " + juce::String(e.settings.candidate + 1)
                                + "  " + juce::StringArray{"Cons", "Creat", "Exper"}[static_cast<int>(e.settings.style)]
                                + "  d=" + juce::String(e.settings.distance, 2), id);
            ++id;
        }
    }
    void recall()
    {
        const int id = history.getSelectedId();
        if (id < 1 || static_cast<std::size_t>(id) > entries.size())
            return;
        const auto entry = entries[static_cast<std::size_t>(id - 1)];
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
            const auto to = static_cast<std::size_t>(target.getSelectedId() - 1);
            message.setText(processor.duplicateVariation(from, to) ? "Duplicated." : "Nothing to duplicate (same scene, identical scenes or preview active).",
                            juce::dontSendNotification);
        };
        resetScene.setButtonText("Reset edited scene to factory");
        resetScene.setTooltip("Restores the factory name and settings of the edited scene. One undo step. Keeps the MIDI note. Disabled during a preview.");
        resetScene.onClick = [this] { processor.resetVariation(access.editingIndex()); message.setText("Scene reset (Undo available).", juce::dontSendNotification); };
        op.addItemList(operationNames, 1);
        op.setSelectedId(1, juce::dontSendNotification);
        op.setTooltip("Effect to reset.");
        resetOp.setButtonText("Reset selected effect");
        resetOp.setTooltip("Resets the chosen effect of the edited scene to off / depth 1 / slice 0.25 beat.");
        resetOp.onClick = [this] {
            const auto index = static_cast<std::size_t>(op.getSelectedId() - 1);
            edit([index](auto& s) { s.operations[index] = gm::OperationSettings{}; });
            message.setText("Effect reset.", juce::dontSendNotification);
        };
        clearPlacement.setButtonText("Clear placement and events");
        clearPlacement.setTooltip("Returns the edited scene to uniform whole-phrase behavior (keeps effect settings, locks and limits).");
        clearPlacement.onClick = [this] {
            edit([](auto& s) { s.control.placement = ctl::PlacementConfig{}; s.control.lane = ctl::EventLane{}; });
            message.setText("Placement cleared.", juce::dontSendNotification);
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
    juce::ComboBox target, op;
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
        setupLabel(returnBarsLabel, "Return after (bars)");
        returnBars.setSliderStyle(juce::Slider::LinearHorizontal);
        returnBars.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
        returnBars.setNumDecimalPlacesToDisplay(0);
        returnBars.setTooltip("0 = off. After a scene switch, switch back after this many bars. Lands on the bar line. A new request, Cancel or a seek cancels it.");
        setupLabel(returnTargetLabel, "Return to");
        returnTarget.addItemList({"Previous scene", "A", "B", "C", "D"}, 1);
        returnTarget.setTooltip("Scene to return to. Previous returns to the scene that was sounding before the switch.");
        setupLabel(returnStatus, "");

        for (auto* c : std::initializer_list<juce::Component*>{&morphTitle, &morphTargetLabel, &morphTarget, &morphAmountLabel,
                                                               &morphAmount, &note, &returnTitle, &returnBarsLabel, &returnBars,
                                                               &returnTargetLabel, &returnTarget, &returnStatus})
            addAndMakeVisible(*c);
        auto& apvts = processor.parameters;
        attachments.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::morphTargetId, morphTarget));
        attachments.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::morphAmountId, morphAmount));
        attachments.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::autoReturnBarsId, returnBars));
        attachments.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::autoReturnTargetId, returnTarget));
    }
    void refresh(const gm::VariationSettings&) override
    {
        const auto t = processor.getAutoReturnTarget();
        returnStatus.setText(t >= 0 ? "Return pending to scene " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + t))
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
        returnTargetLabel.setBounds(r.removeFromLeft(120));
        returnTarget.setBounds(r.removeFromLeft(150));
        returnStatus.setBounds(nextRow(area));
    }
private:
    juce::Label morphTitle, morphTargetLabel, morphAmountLabel, note, returnTitle, returnBarsLabel, returnTargetLabel, returnStatus;
    juce::ComboBox morphTarget, returnTarget;
    juce::Slider morphAmount, returnBars;
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
        maxSlider.setup(*this, "Maximum at once", "1-5 effects may be active together. Only enabled effects count.", 1, 5, 1, 0);
        maxSlider.commit = [this] { edit([this](auto& s) { s.control.activity.maxConcurrent = static_cast<std::uint8_t>(maxSlider.slider.getValue()); }); };
        for (std::size_t i = 0; i < gm::operationCount; ++i)
        {
            prio[i].setup(*this, operationNames[static_cast<int>(i)] + " priority", "0-100. Higher priority survives when the limit is exceeded; ties keep the earlier effect in chain order.", 0, 100, 1, 0);
            prio[i].commit = [this, i] { edit([this, i](auto& s) { s.control.activity.priority[i] = static_cast<std::uint8_t>(prio[i].slider.getValue()); }); };
        }
        setupLabel(note, "Applies live to the scene being played. Not implemented in this alpha: minimum event spacing and maximum phrase coverage limits.");
        note.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(enable);
        addAndMakeVisible(note);
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
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        enable.setBounds(nextRow(area));
        maxSlider.layout(nextRow(area), 150);
        for (auto& p : prio)
            p.layout(nextRow(area), 190);
        note.setBounds(nextRow(area, 60));
    }
private:
    juce::ToggleButton enable;
    juce::Label note;
    SliderRow maxSlider;
    std::array<SliderRow, gm::operationCount> prio;
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
        text.setText(
            "ALPHA FEATURE STATUS\n\n"
            "Working (see docs/alpha/ALPHA-FEATURE-STATUS.md):\n"
            " - Phrase placement: window, every-N cadence, origin, snap grid, downbeat protect\n"
            " - Event lanes: per-effect events (max 32/scene), drag/resize/nudge/delete\n"
            " - Generator: seed, style, distance, candidates, locks, preview/apply/revert, history\n"
            " - Scenes: duplicate, reset, copy FX, rename, undo/redo\n"
            " - Scene morph and automatic return (host parameters)\n"
            " - Effect activity limit (max at once + priority)\n"
            " - Host Variation parameter follows the sounding scene\n"
            " - Demo loop (replaces input in Standalone for evaluation)\n\n"
            "Not implemented in this alpha:\n"
            " - C04 transition phase options\n"
            " - Per-event phase restart / cycle anchors; spacing and coverage limits\n"
            " - Low-frequency (bass) protection; multiband processing\n"
            " - Performance mode macros, phrase evolution, polyrhythmic lanes\n"
            " - Audio-triggered actions, audio import/capture, gesture recording\n"
            " - CLAP and additional formats\n\n"
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
          status(std::make_unique<StatusTab>()),
          tabs(juce::TabbedButtonBar::TabsAtTop)
    {
        const auto bg = juce::Colour(0xff1b232e);
        tabs.addTab("Placement", bg, placement.get(), false);
        tabs.addTab("Generator", bg, generator.get(), false);
        tabs.addTab("Scenes", bg, scenes.get(), false);
        tabs.addTab("Morph/Return", bg, morph.get(), false);
        tabs.addTab("Limits", bg, limits.get(), false);
        tabs.addTab("Status", bg, status.get(), false);
    }
    SceneEditAccess access;
    std::unique_ptr<PlacementTab> placement;
    std::unique_ptr<GeneratorTab> generator;
    std::unique_ptr<ScenesTab> scenes;
    std::unique_ptr<MorphTab> morph;
    std::unique_ptr<LimitsTab> limits;
    std::unique_ptr<StatusTab> status;
    juce::TabbedComponent tabs;
};

InspectorPanel::InspectorPanel(GrooveMorphAudioProcessor& processor, SceneEditAccess access)
    : impl(std::make_unique<Impl>(processor, std::move(access)))
{
    addAndMakeVisible(impl->tabs);
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
}
} // namespace groovemorph::ui
