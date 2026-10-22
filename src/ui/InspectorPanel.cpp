#include "ui/InspectorPanel.h"
#include "ui/ControlMenu.h"
#include "ui/EuclidPanel.h"
#include "ui/WaveformView.h"
#include "ui/PrintPanel.h"
#include "ui/theme/Theme.h"
#include "ui/visual/Glyphs.h"

#include "PluginProcessor.h"
#include "core/control/PhraseFit.h"
#include "core/control/PlacementEvaluator.h"
#include "core/control/SceneControl.h"
#include "core/scene/SceneSerializer.h"
#include "core/variation/CandidateBuilder.h"
#include "core/variation/GeneratorHistory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <functional>
#include <iomanip>
#include <iterator>
#include <vector>
#include <sstream>

namespace gm = groovemorph::core;
namespace ctl = groovemorph::core::control;
namespace gvar = groovemorph::core::variation;
namespace viz = groovemorph::core::viz;

namespace groovemorph::ui
{
namespace
{
const juce::StringArray operationNames{"Repeat / stutter", "Reverse slice", "Gate", "Half-time", "Tape stop", "Filter sweep", "Bit crush"};
static_assert(theme::effectCount == static_cast<int>(groovemorph::core::operationCount), "one theme colour per operation");
constexpr std::uint8_t snapValues[] = {1, 2, 3, 4, 6, 8, 12, 16};

// Lane index (processor order) to the shared glyph enum of the Visual window (Live = 0 comes first there).
viz::Effect glyphEffect(int lane) { return static_cast<viz::Effect>(std::clamp(lane, 0, 6) + 1); }

juce::Rectangle<int> takeRow(juce::Rectangle<int>& area, int height = theme::minTarget)
{
    auto row = area.removeFromTop(height);
    area.removeFromTop(theme::space1);
    return row;
}

void setupLabel(juce::Label& label, const juce::String& text, const juce::String& tip = {})
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(theme::font(theme::fontLabel));
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, theme::text);
    if (tip.isNotEmpty())
        label.setTooltip(tip);
}

// Height of wrapped text at a given width (layout time only, never called from paint()).
int wrappedHeight(const juce::String& text, const juce::Font& font, int width)
{
    if (text.isEmpty() || width <= 0)
        return 0;
    juce::AttributedString as;
    as.setText(text);
    as.setFont(font);
    juce::TextLayout layout;
    layout.createLayout(as, static_cast<float>(width));
    return static_cast<int>(std::ceil(layout.getHeight())) + 4;
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
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 26);
        slider.setRange(lo, hi, step);
        slider.setNumDecimalPlacesToDisplay(decimals);
        slider.setTooltip(tip);
        if (text.isNotEmpty())
            slider.setTitle(text);
        slider.onDragStart = [this] { dragging = true; };
        slider.onDragEnd = [this] { dragging = false; if (commit) commit(); };
        slider.onValueChange = [this] { if (!dragging && commit) commit(); };
        parent.addAndMakeVisible(label);
        parent.addAndMakeVisible(slider);
    }
    // Takes one row from `area`: label left of the slider, or (stacked) label above the slider.
    void place(juce::Rectangle<int>& area, int labelWidth = 132, bool stacked = false)
    {
        if (stacked)
        {
            label.setBounds(area.removeFromTop(16));
            slider.setBounds(area.removeFromTop(theme::minTarget));
            area.removeFromTop(theme::space1);
            return;
        }
        auto row = takeRow(area);
        label.setBounds(row.removeFromLeft(labelWidth));
        slider.setBounds(row);
    }
    void set(double value)
    {
        if (!slider.isMouseButtonDown() && !slider.hasKeyboardFocus(true))
            slider.setValue(value, juce::dontSendNotification);
    }
};

// Content of one scrolling tab page: paints the cards (surface colour, 12 px title) behind the controls.
struct Page final : public juce::Component
{
    struct Card
    {
        juce::Rectangle<int> bounds;
        juce::String title;
    };
    std::vector<Card> cards;
    std::function<void(juce::Graphics&)> extraPaint;

    Page() { setOpaque(true); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme::bg);
        for (const auto& card : cards)
        {
            const auto r = card.bounds.toFloat();
            g.setColour(theme::surface);
            g.fillRoundedRectangle(r, theme::radius);
            g.setColour(theme::stroke);
            g.drawRoundedRectangle(r.reduced(0.5f), theme::radius, 1.0f);
            g.setColour(theme::text);
            g.setFont(theme::font(theme::fontLabel, true));
            g.drawText(card.title, card.bounds.getX() + theme::space2, card.bounds.getY() + theme::space2,
                       card.bounds.getWidth() - 2 * theme::space2, 16, juce::Justification::centredLeft, true);
        }
        if (extraPaint)
            extraPaint(g);
    }
    // Keyboard users tab into controls that may be scrolled out of view: bring the focused one into view.
    void focusOfChildComponentChanged(FocusChangeType) override
    {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        auto* viewport = findParentComponentOfClass<juce::Viewport>();
        if (focused == nullptr || viewport == nullptr || focused == this || !isParentOf(focused))
            return;
        const auto r = getLocalArea(focused, focused->getLocalBounds());
        const int top = r.getY() - theme::space2, bottom = r.getBottom() + theme::space2;
        const int viewY = viewport->getViewPositionY(), viewH = viewport->getViewHeight();
        if (r.getHeight() >= viewH)
        {
            if (bottom < viewY || top > viewY + viewH) // a component taller than the view only scrolls when fully hidden
                viewport->setViewPosition(viewport->getViewPositionX(), std::max(0, top));
        }
        else if (top < viewY)
            viewport->setViewPosition(viewport->getViewPositionX(), std::max(0, top));
        else if (bottom > viewY + viewH)
            viewport->setViewPosition(viewport->getViewPositionX(), bottom - viewH);
    }
};

// Places cards into 1 to 3 columns (always into the shortest one) and reports the content height.
class CardLayout
{
public:
    static int columnsFor(int width) noexcept { return width >= 1500 ? 3 : width >= 700 ? 2 : 1; }

    CardLayout(Page& p, int x, int y, int width, int columnCount) : page(p), left(x), columns(std::max(1, columnCount))
    {
        columnWidth = std::max(40, (width - (columns - 1) * theme::space3) / columns);
        totalWidth = columnWidth * columns + (columns - 1) * theme::space3;
        ys.assign(static_cast<std::size_t>(columns), y);
    }
    // Starts a card (in the shortest column, or across all columns) and returns its content area, which is
    // tall; take rows with takeRow() or SliderRow::place(), which keep the trailing gap end() relies on.
    juce::Rectangle<int> begin(const juce::String& title, bool fullWidth = false)
    {
        activeTitle = title;
        activeFull = fullWidth;
        if (fullWidth)
        {
            startY = *std::max_element(ys.begin(), ys.end());
            activeX = left;
            activeW = totalWidth;
        }
        else
        {
            column = static_cast<std::size_t>(std::min_element(ys.begin(), ys.end()) - ys.begin());
            startY = ys[column];
            activeX = left + static_cast<int>(column) * (columnWidth + theme::space3);
            activeW = columnWidth;
        }
        return {activeX + theme::space2, startY + headerHeight, activeW - 2 * theme::space2, 100000};
    }
    void end(const juce::Rectangle<int>& content)
    {
        const int bottom = std::max(startY + headerHeight, content.getY() - theme::space1) + theme::space2;
        page.cards.push_back({{activeX, startY, activeW, bottom - startY}, activeTitle});
        if (activeFull)
            std::fill(ys.begin(), ys.end(), bottom + theme::space3);
        else
            ys[column] = bottom + theme::space3;
    }
    // Bottom edge of everything placed so far.
    int bottom() const { return *std::max_element(ys.begin(), ys.end()) - theme::space3; }

private:
    static constexpr int headerHeight = 30;
    Page& page;
    int left = 0, columns = 1, columnWidth = 0, totalWidth = 0;
    std::vector<int> ys;
    std::size_t column = 0;
    int startY = 0, activeX = 0, activeW = 0;
    bool activeFull = false;
    juce::String activeTitle;
};

// Wrapping row of fixed-width controls (FlexBox). Consumes the rows it used from `area`.
void flowRow(juce::Rectangle<int>& area, const std::vector<std::pair<juce::Component*, int>>& items)
{
    juce::FlexBox box;
    box.flexDirection = juce::FlexBox::Direction::row;
    box.flexWrap = juce::FlexBox::Wrap::wrap;
    box.justifyContent = juce::FlexBox::JustifyContent::flexStart;
    box.alignContent = juce::FlexBox::AlignContent::flexStart;
    for (const auto& item : items)
        box.items.add(juce::FlexItem(*item.first)
                          .withWidth(static_cast<float>(item.second))
                          .withHeight(static_cast<float>(theme::minTarget))
                          .withMargin(juce::FlexItem::Margin(0.0f, static_cast<float>(theme::space1), static_cast<float>(theme::space1), 0.0f)));
    box.performLayout(juce::Rectangle<float>(static_cast<float>(area.getX()), static_cast<float>(area.getY()),
                                             static_cast<float>(area.getWidth()), 2000.0f));
    float bottom = static_cast<float>(area.getY());
    for (const auto& item : box.items)
        bottom = std::max(bottom, item.currentBounds.getBottom());
    area.removeFromTop(static_cast<int>(std::ceil(bottom)) - area.getY() + theme::space1);
}

// Equal-width cells in one row (Grid).
void gridRow(juce::Rectangle<int> row, std::initializer_list<juce::Component*> items)
{
    juce::Grid grid;
    for (std::size_t i = 0; i < items.size(); ++i)
        grid.templateColumns.add(juce::Grid::TrackInfo(juce::Grid::Fr(1)));
    grid.templateRows.add(juce::Grid::TrackInfo(juce::Grid::Fr(1)));
    grid.columnGap = juce::Grid::Px(theme::space1);
    for (auto* c : items)
        grid.items.add(juce::GridItem(*c));
    grid.performLayout(row);
}

// Keyboard focus and a tooltip of at least 12 characters for every interactive control below `root`.
void polish(juce::Component& root)
{
    for (auto* child : root.getChildren())
    {
        const bool interactive = dynamic_cast<juce::Slider*>(child) != nullptr || dynamic_cast<juce::ComboBox*>(child) != nullptr
                                 || dynamic_cast<juce::Button*>(child) != nullptr || dynamic_cast<juce::TextEditor*>(child) != nullptr;
        if (interactive)
        {
            child->setWantsKeyboardFocus(true);
            if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(child))
                if (client->getTooltip().length() < 12)
                {
                    juce::String name = child->getTitle().isNotEmpty() ? child->getTitle() : child->getName();
                    if (auto* button = dynamic_cast<juce::Button*>(child))
                        if (button->getButtonText().isNotEmpty())
                            name = button->getButtonText();
                    client->setTooltip((name.isNotEmpty() ? name + ": " : juce::String()) + "change this setting with the mouse or keyboard.");
                }
            continue; // the parts inside a slider or combo box are not ours
        }
        polish(*child);
    }
}

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

    InspectorTab(GrooveMorphAudioProcessor& p, SceneEditAccess a) : processor(p), access(std::move(a))
    {
        viewport.setViewedComponent(&page, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(10);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);
    }
    virtual void refresh(const gm::VariationSettings& settings) = 0;
    void resized() override
    {
        viewport.setBounds(getLocalBounds());
        relayout();
    }
    void polishControls() { polish(page); }

protected:
    GrooveMorphAudioProcessor& processor;
    SceneEditAccess access;
    bool refreshing = false;
    Page page;
    juce::Viewport viewport;

    // Positions every child of `page` for a viewport of viewW x viewH and returns the content height.
    virtual int layoutPage(int viewW, int viewH) = 0;
    void add(juce::Component& child) { page.addAndMakeVisible(child); }
    void relayout()
    {
        const int viewW = viewport.getWidth(), viewH = viewport.getHeight();
        if (viewW <= 0 || viewH <= 0)
            return;
        page.cards.clear();
        int width = viewW;
        int height = layoutPage(width, viewH);
        if (height > viewH)
        {
            width = viewW - viewport.getScrollBarThickness();
            page.cards.clear();
            height = layoutPage(width, viewH);
        }
        page.setSize(width, std::max(height, viewH));
        page.repaint();
    }

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
                   "creates an event there; Escape deselects. An event's height shows its chance of playing; "
                   "a dotted top edge means below 100 percent.");
        setTitle("Event lanes");
        setDescription("Event lanes, one row per effect. Arrow keys move a cursor, Enter creates an event.");
        for (std::size_t i = 0; i < numberLabels.size(); ++i)
            numberLabels[i] = juce::String(static_cast<int>(i));
        for (std::size_t i = 0; i < percentLabels.size(); ++i)
            percentLabels[i] = juce::String(static_cast<int>(i)) + "%";
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
                ? juce::String(fit.affected()) + " event(s) extend past the phrase end and wrap around. Limits tab, Events x2 or /2, rescales them."
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

    // Smallest height that still gives every lane its minimum height.
    static int minimumHeight() noexcept { return laneTop + laneCount * minLaneHeight + 2; }

    void resized() override
    {
        laneHeight = std::max(minLaneHeight, (getHeight() - laneTop - 2) / laneCount);
    }

    void paint(juce::Graphics& g) override
    {
        const auto grid = processor.getGridContext();
        const double P = static_cast<double>(grid.phraseTicks());
        const auto area = getLocalBounds();
        g.fillAll(theme::bg);
        const int left = labelWidth;
        const int width = std::max(1, area.getWidth() - left - 4);
        const int lanesBottom = laneTop + laneCount * laneHeight;
        const int beatsPerBar = std::max(1, grid.beatsPerBar);
        const auto& control = draft;

        // Ruler background and label column.
        g.setColour(theme::surface);
        g.fillRect(0, 0, area.getWidth(), laneTop);
        g.fillRect(0, laneTop, left, area.getHeight() - laneTop);

        // Lanes: alternating stripes, then the lane label (glyph + name) in the label column.
        for (int lane = 0; lane < laneCount; ++lane)
        {
            const int y = laneTop + lane * laneHeight;
            g.setColour(lane % 2 ? theme::surfaceRaised.withAlpha(0.55f) : theme::surface.withAlpha(0.55f));
            g.fillRect(left, y, area.getWidth() - left, laneHeight);
            g.setColour(theme::stroke);
            g.drawHorizontalLine(y + laneHeight - 1, 0.0f, static_cast<float>(area.getWidth()));

            const auto colour = theme::effectColour(lane);
            const float glyphSize = 22.0f;
            visual::drawGlyph(g, glyphEffect(lane),
                              juce::Rectangle<float>(8.0f, static_cast<float>(y) + (static_cast<float>(laneHeight) - glyphSize) * 0.5f, glyphSize, glyphSize),
                              colour, 2.0f);
            const bool laneLocked = (control.permissions.operationLocks[static_cast<std::size_t>(lane)] & ctl::lock::events) != 0;
            g.setColour(theme::text);
            g.setFont(theme::font(theme::fontLabel));
            const int textX = 8 + static_cast<int>(glyphSize) + 6;
            if (laneLocked)
            {
                g.drawText(theme::effectName(lane), textX, y + 3, left - textX - 4, laneHeight / 2 - 2, juce::Justification::bottomLeft, true);
                g.setColour(theme::warning);
                g.setFont(theme::font(theme::fontCaption));
                g.drawText("locked", textX, y + laneHeight / 2 + 1, left - textX - 4, laneHeight / 2 - 3, juce::Justification::topLeft, true);
            }
            else
                g.drawText(theme::effectName(lane), textX, y, left - textX - 4, laneHeight, juce::Justification::centredLeft, true);

            if (!control.lane.explicitEvents)
            {
                const auto& op = view.operations[static_cast<std::size_t>(lane)];
                if (op.enabled)
                {
                    g.setColour(colour.withAlpha(0.22f));
                    g.fillRect(left, y + 3, width, laneHeight - 6);
                }
            }
        }
        g.setColour(theme::strokeStrong);
        g.drawVerticalLine(left - 1, 0.0f, static_cast<float>(lanesBottom));

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
            g.setColour(theme::accent.withAlpha(0.14f));
            for (int x = 0; x < width; ++x)
            {
                const double beats = base + originBeats + (x + 0.5) / width * phraseBeats;
                if (ctl::evaluate(windowOnly, grid, beats, 0u).active[0])
                    g.fillRect(left + x, laneTop, 1, laneCount * laneHeight);
            }
        }

        // Ruler: bar numbers, beat numbers and snap subdivisions.
        const int beats = grid.beatsPerBar * grid.phraseBars;
        const int subdivision = std::max<int>(1, control.placement.subdivision);
        const bool beatLabels = beats > 0 && width / beats >= 30;
        for (int b = 0; b <= beats; ++b)
        {
            const bool bar = b % beatsPerBar == 0;
            const float x = left + static_cast<float>(b) / static_cast<float>(std::max(1, beats)) * width;
            g.setColour(bar ? theme::textDim : theme::strokeStrong);
            g.drawVerticalLine(static_cast<int>(x), static_cast<float>(bar ? 2 : laneTop - 8),
                               static_cast<float>(lanesBottom));
            if (b < beats && (bar || beatLabels))
            {
                g.setColour(bar ? theme::text : theme::textDim);
                g.setFont(theme::font(bar ? theme::fontLabel : theme::fontCaption, bar));
                g.drawText(number(bar ? b / beatsPerBar + 1 : b % beatsPerBar + 1), static_cast<int>(x) + 3, bar ? 2 : 4, 28,
                           bar ? 16 : 14, juce::Justification::centredLeft, false);
            }
            if (b < beats && width / std::max(1, beats) / subdivision >= 4)
            {
                g.setColour(theme::stroke);
                for (int s = 1; s < subdivision; ++s)
                    g.drawVerticalLine(static_cast<int>(x + static_cast<float>(s) * width / static_cast<float>(beats) / static_cast<float>(subdivision)),
                                       static_cast<float>(laneTop), static_cast<float>(lanesBottom));
            }
        }

        // Events: height shows the chance of playing, a dotted top edge marks anything below 100 percent.
        if (control.lane.explicitEvents)
        {
            const float innerHeight = static_cast<float>(laneHeight - 8);
            for (std::size_t i = 0; i < control.lane.count; ++i)
            {
                const auto& e = control.lane.events[i];
                const int op = static_cast<int>(e.operation % gm::operationCount);
                const auto colour = theme::effectColour(op);
                const int laneChancePct = control.lane.chanceByOp[static_cast<std::size_t>(op)];
                const int effective = std::clamp(static_cast<int>(e.chance) * laneChancePct / 100, 0, 100); // N03
                const float eventHeight = std::max(6.0f, innerHeight * static_cast<float>(effective) / 100.0f);
                const float eventTop = static_cast<float>(laneTop + op * laneHeight + 4) + (innerHeight - eventHeight);
                const bool isSelected = static_cast<int>(i) == selectedIndex;
                const auto ranges = eventRanges(e, P, width);
                for (int k = 0; k < ranges.count; ++k)
                {
                    const auto& r = ranges.range[static_cast<std::size_t>(k)];
                    const juce::Rectangle<float> rect(static_cast<float>(left + r.first), eventTop,
                                                      static_cast<float>(std::max(2, r.second - r.first)), eventHeight);
                    g.setColour(colour.withAlpha(e.enabled ? 0.35f + 0.55f * e.depth : 0.15f));
                    g.fillRoundedRectangle(rect, 3.0f);
                    if (effective < 100)
                    {
                        const float dashes[] = {2.0f, 3.0f};
                        g.setColour(theme::text);
                        g.drawDashedLine(juce::Line<float>(rect.getX(), rect.getY() + 1.0f, rect.getRight(), rect.getY() + 1.0f), dashes, 2, 2.0f);
                        if (rect.getWidth() > 38.0f && rect.getHeight() >= 16.0f)
                        {
                            g.setColour(theme::bg.withAlpha(0.7f));
                            g.fillRoundedRectangle(rect.getCentreX() - 17.0f, rect.getCentreY() - 8.0f, 34.0f, 16.0f, 3.0f);
                            g.setColour(theme::text);
                            g.setFont(theme::font(theme::fontCaption));
                            g.drawText(percent(effective), rect.toNearestInt(), juce::Justification::centred, false);
                        }
                    }
                    g.setColour(isSelected ? theme::playhead : colour);
                    g.drawRoundedRectangle(rect, 3.0f, isSelected ? 2.0f : 1.0f);
                    if (rect.getWidth() < static_cast<float>(narrowEventPixels))
                        g.fillRect(rect.getX() - 2.0f, rect.getY() - 3.0f, rect.getWidth() + 4.0f, 3.0f); // grip: drag to move
                    if (e.locked)
                    {
                        g.setColour(theme::text);
                        g.fillRect(rect.getX() + 3.0f, rect.getY() + 3.0f, 5.0f, 5.0f); // per-event lock mark
                    }
                }
            }
        }

        // Protected regions (C03): no effect sounds inside them.
        for (std::size_t i = 0; i < control.placement.regionCount && i < ctl::maxProtectedRegions; ++i)
        {
            const auto ranges = regionRanges(control.placement.regions[i], P, width);
            g.setColour(theme::danger.withAlpha(0.25f));
            for (int k = 0; k < ranges.count; ++k)
            {
                const auto& r = ranges.range[static_cast<std::size_t>(k)];
                g.fillRect(left + r.first, laneTop, std::max(2, r.second - r.first), laneCount * laneHeight);
            }
        }

        // Downbeat protection marks.
        if (control.placement.downbeatProtectBeats > 0.0f)
        {
            g.setColour(theme::danger.withAlpha(0.6f));
            for (int bar = 0; bar < grid.phraseBars; ++bar)
            {
                const float x = left + static_cast<float>(bar * grid.beatsPerBar) / static_cast<float>(std::max(1, beats)) * width;
                const float w = std::min<float>(control.placement.downbeatProtectBeats, static_cast<float>(grid.beatsPerBar))
                                / static_cast<float>(std::max(1, beats)) * width;
                g.fillRect(x, static_cast<float>(laneTop - 5), std::max(1.0f, w), 4.0f);
            }
        }

        // Playhead (position inside the phrase, shifted by the origin): 2 px white line.
        if (processor.hasValidTiming() && processor.isTransportPlaying())
        {
            double ticks = processor.getBarInPhrase() * grid.beatsPerBar * ctl::ticksPerBeat
                           - static_cast<double>(control.placement.originTicks);
            ticks = std::fmod(std::fmod(ticks, P) + P, P);
            g.setColour(theme::playhead);
            g.fillRect(left + static_cast<int>(ticks / P * width) - 1, 0, 2, lanesBottom);
        }
        // Keyboard cursor (shown while the canvas has focus and no event is selected).
        if (hasKeyboardFocus(false) && selectedIndex < 0 && control.lane.explicitEvents)
        {
            const int cx = left + static_cast<int>(static_cast<double>(std::clamp<std::int64_t>(kbTick, 0, grid.phraseTicks())) / P * width);
            const int cy = laneTop + std::clamp(kbLane, 0, laneCount - 1) * laneHeight;
            g.setColour(theme::focusRing);
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
                g.setFont(theme::font(theme::fontLabel));
                const int badgeWidth = std::min(area.getWidth() - 8, static_cast<int>(text.length()) * 7 + 16);
                const auto badge = juce::Rectangle<int>(area.getRight() - badgeWidth - 4, 3, badgeWidth, 16);
                g.setColour(theme::warning);
                g.fillRoundedRectangle(badge.toFloat(), 4.0f);
                g.setColour(theme::textOnAccent);
                g.drawText(text, badge, juce::Justification::centred);
            }
        }
        if (hasKeyboardFocus(false))
        {
            g.setColour(theme::focusRing);
            g.drawRect(getLocalBounds(), 2);
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
        if (event.y < laneTop || lane < 0 || lane >= laneCount || event.x < labelWidth)
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
                    onMessage("This lane's events are locked (Generate tab > Locks).");
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
                    onMessage("This lane's events are locked (Generate tab > Locks).");
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
        if (event.y < laneTop || lane < 0 || lane >= laneCount)
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
    static constexpr int minLaneHeight = 36;   // lanes grow with the canvas, never below this
    static constexpr int laneTop = 22;         // ruler height
    static constexpr int labelWidth = 120;     // glyph + effect name column
    int laneHeight = minLaneHeight;
    std::array<juce::String, 130> numberLabels; // ruler numbers and chance labels, built once (no string work in paint)
    std::array<juce::String, 101> percentLabels;
    const juce::String& number(int n) const noexcept { return numberLabels[static_cast<std::size_t>(std::clamp(n, 0, 129))]; }
    const juce::String& percent(int n) const noexcept { return percentLabels[static_cast<std::size_t>(std::clamp(n, 0, 100))]; }
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
            announceCursor("This lane's events are locked (Generate tab > Locks).");
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
            announceCursor(status == ctl::EditStatus::Capacity ? "Event limit reached (" + juce::String(static_cast<int>(ctl::maxLaneEvents)) + ")."
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
    // Pixel ranges of one event; wrapping events split in two (fixed size, no allocation in paint()).
    struct Ranges
    {
        std::array<std::pair<int, int>, 2> range{};
        int count = 0;
    };
    static Ranges eventRanges(const ctl::LaneEvent& e, double P, int width)
    {
        Ranges ranges;
        // Events saved for a longer phrase wrap into this one, exactly like the audio side.
        const double a = std::fmod(static_cast<double>(e.onsetTicks), P);
        const double b = a + std::min(static_cast<double>(e.durationTicks), P);
        if (b <= P)
            ranges.range[ranges.count++] = {static_cast<int>(a / P * width), static_cast<int>(b / P * width)};
        else
        {
            ranges.range[ranges.count++] = {static_cast<int>(a / P * width), width};
            ranges.range[ranges.count++] = {0, static_cast<int>((b - P) / P * width)};
        }
        return ranges;
    }
    static Ranges regionRanges(const ctl::ProtectedRegion& region, double P, int width)
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
// Pattern tab: placement window, event lanes, Euclid drawer, loop waveform
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
        start.setup(page, "Window start", "Custom window start as a fraction of the phrase (0 = phrase start).", 0, 0.999, 0.001, 3);
        start.commit = [this] { edit([this](auto& s) { s.control.placement.startPhase = static_cast<float>(start.slider.getValue()); }); };
        span.setup(page, "Window span", "Custom window length as a fraction of the phrase (1 = whole phrase). Start + span may wrap.", 0.001, 1, 0.001, 3);
        span.commit = [this] { edit([this](auto& s) { s.control.placement.spanPhase = static_cast<float>(span.slider.getValue()); }); };
        every.setup(page, "Every N phrases", "Act on one phrase out of every N (1 = every phrase).", 1, 64, 1, 0);
        every.commit = [this] { edit([this](auto& s) { s.control.placement.every = static_cast<std::uint8_t>(every.slider.getValue()); }); };
        offset.setup(page, "Phrase offset", "Which phrase of each N-phrase cycle is active (0 = first). Counted from the grid origin.", 0, 63, 1, 0);
        offset.commit = [this] { edit([this](auto& s) { s.control.placement.cadenceOffset = static_cast<std::uint8_t>(offset.slider.getValue()); }); };
        origin.setup(page, "Phrase origin (beats)", "Shifts the placement timeline against the host/phrase grid, in meter beats. Positive moves events later.", -16, 16, 1.0 / 16.0, 3);
        origin.commit = [this] { edit([this](auto& s) { s.control.placement.originTicks = static_cast<std::int32_t>(std::lround(origin.slider.getValue() * ctl::ticksPerBeat)); }); };
        protect.setup(page, "Protect downbeat (beats)", "Silences all effects for this many beats at the start of every bar (0 = off), keeping the groove anchor intact.", 0, 4, 0.25, 2);
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
        eventDepth.setup(page, "Event depth", "Scales the operation depth inside the selected event.", 0, 1, 0.01, 2);
        eventDepth.commit = [this] {
            const int i = canvas.selected();
            if (i < 0 || refreshing)
                return;
            edit([this, i](auto& s) {
                if (static_cast<std::size_t>(i) < s.control.lane.count)
                    s.control.lane.events[static_cast<std::size_t>(i)].depth = static_cast<float>(eventDepth.slider.getValue());
            });
        };
        eventSlice.setup(page, "Event slice (beats)", "0 = use the effect's own slice. Otherwise this event uses its own slice length (1/16 - 4 beats) while it is active.", 0, 4, 1.0 / 16.0, 3);
        eventSlice.slider.textFromValueFunction = [](double v) { return v < 0.001 ? juce::String("effect") : juce::String(v, 3); };
        eventSlice.commit = [this] {
            const int i = canvas.selected();
            if (i < 0 || refreshing) return;
            edit([this, i](auto& s) {
                if (static_cast<std::size_t>(i) < s.control.lane.count)
                    s.control.lane.events[static_cast<std::size_t>(i)].sliceBeats = static_cast<float>(eventSlice.slider.getValue());
            });
        };
        eventPriority.setup(page, "Event priority", "0-100. When the 'effects at once' limit is exceeded, higher-priority events keep playing (Limits tab).", 0, 100, 1, 0);
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
        loopBeats.setup(page, "", "Loop length of the chosen lane's events in meter beats. 0 = follow the phrase. A lane with 3 beats and another with 4 beats drift against each other (polyrhythm).", 0, 64, 1, 0);
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
        canvas.onMessage = [this](const juce::String& m) { message.setText(m, juce::dontSendNotification); relayout(); };
        setupLabel(message, "");
        message.setColour(juce::Label::textColourId, theme::warning);
        setupLabel(waveLabel, "Loop", "Overview of the loaded loop, aligned with the lanes below.");
        waveform.setVisible(false);
        waveLabel.setVisible(false);
        for (auto* c : std::initializer_list<juce::Component*>{&enable, &window, &snap, &snapLabel, &modeButton, &convert,
                                                               &removeSelected, &eventEnabled, &message, &canvas, &eventRestart, &eventLocked, &loopLane})
            add(*c);
        page.addChildComponent(waveform);
        page.addChildComponent(waveLabel);
        // N03: Euclid generator and chance panel, swapped in over the lower event rows (EuclidPanel.cpp).
        euclidToggle.setButtonText("Euclid / chance");
        euclidToggle.setClickingTogglesState(true);
        euclidToggle.setTooltip("Euclidean pattern generator and per-event / per-lane chance. Opens a drawer next to the lanes (over them in a narrow window).");
        euclidToggle.onClick = [this] {
            euclid.setVisible(euclidToggle.getToggleState());
            relayout();
            euclid.toFront(false);
        };
        add(euclidToggle);
        page.addChildComponent(euclid);
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
            message.setText(locked ? "Placement is locked (Generate tab > Locks)." : "", juce::dontSendNotification);
            relayout();
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
        // Loop waveform strip (same processor data as the Input tab); only present while a loop is loaded.
        // With no loop (or no peaks yet) the strip collapses to zero height and shows no label.
        std::vector<float> lo, hi;
        const bool loop = processor.hasLoop() && processor.getLoopPeaks(waveform.preferredColumns(), lo, hi) > 0 && !lo.empty();
        if (loop != loopShown)
        {
            loopShown = loop;
            waveform.setVisible(loop);
            waveLabel.setVisible(loop);
            relayout();
        }
        if (loop && isShowing())
        {
            waveform.setPeaks(lo, hi);
            waveform.setGrid(processor.getLoopBeats(), processor.getLoopOriginBeats(), processor.getGridContext().beatsPerBar);
            waveform.setPlayhead(processor.getLoopPlayFraction());
        }
    }

protected:
    int layoutPage(int viewW, int viewH) override
    {
        const int pad = theme::space2, gap = theme::space3;
        const bool wide = viewW >= wideBreakpoint;
        const int mainW = wide ? viewW - 2 * pad - rightColumnWidth - gap : viewW - 2 * pad;

        // Toolbar: snap grid, event-lane mode, convert, delete, Euclid drawer toggle (wraps in a narrow window).
        juce::FlexBox bar;
        bar.flexDirection = juce::FlexBox::Direction::row;
        bar.flexWrap = juce::FlexBox::Wrap::wrap;
        bar.justifyContent = juce::FlexBox::JustifyContent::flexStart;
        bar.alignContent = juce::FlexBox::AlignContent::flexStart;
        const auto addItem = [&bar](juce::Component& c, int w) {
            bar.items.add(juce::FlexItem(c).withWidth(static_cast<float>(w)).withHeight(static_cast<float>(toolbarHeight))
                              .withMargin(juce::FlexItem::Margin(0.0f, static_cast<float>(theme::space1), 0.0f, 0.0f)));
        };
        addItem(snapLabel, 70);
        addItem(snap, 110);
        addItem(modeButton, 140);
        addItem(convert, 140);
        addItem(removeSelected, 110);
        addItem(euclidToggle, 130);
        bar.performLayout(juce::Rectangle<float>(static_cast<float>(pad), static_cast<float>(pad), static_cast<float>(mainW), 400.0f));
        float barBottom = static_cast<float>(pad), lastRight = static_cast<float>(pad), lastTop = static_cast<float>(pad);
        for (const auto& item : bar.items)
        {
            barBottom = std::max(barBottom, item.currentBounds.getBottom());
            lastRight = item.currentBounds.getRight();
            lastTop = item.currentBounds.getY();
        }
        int y = static_cast<int>(std::ceil(barBottom)) + theme::space1;
        const int messageX = static_cast<int>(lastRight) + theme::space2;
        if (pad + mainW - messageX >= 110)
            message.setBounds(messageX, static_cast<int>(lastTop), pad + mainW - messageX, toolbarHeight);
        else
        {
            // No room beside the buttons: show the message only while there is one, so it never pushes the lanes
            // down for nothing.
            const int messageHeight = message.getText().isEmpty() ? 0 : 20;
            message.setBounds(pad, y, mainW, messageHeight);
            if (messageHeight > 0)
                y += messageHeight + theme::space1;
        }

        // Loop waveform strip above the ruler, aligned with the lane timeline.
        const int regionTop = y;
        const int labelColumn = 120;
        if (loopShown)
        {
            waveLabel.setBounds(pad + 8, y, labelColumn - 12, waveformHeight);
            waveform.setBounds(pad + labelColumn, y, std::max(40, mainW - labelColumn - 4 - (wide && euclid.isVisible() ? drawerWidth + gap : 0)), waveformHeight);
            y += waveformHeight + theme::space1;
        }

        // Lanes fill the remaining height (never below 36 px per lane); a short window scrolls.
        const int canvasHeight = std::max(LaneCanvas::minimumHeight(), viewH - y - pad);
        const bool drawerBeside = wide && euclid.isVisible();
        const int canvasWidth = mainW - (drawerBeside ? drawerWidth + gap : 0);
        canvas.setBounds(pad, y, canvasWidth, canvasHeight);
        const int regionBottom = y + canvasHeight;
        const int drawerW = std::min(drawerWidth, mainW);
        euclid.setBounds(pad + mainW - drawerW, regionTop, drawerW, regionBottom - regionTop);
        if (euclid.isVisible())
            euclid.toFront(false);
        int contentBottom = regionBottom;

        const auto eventCard = [this](CardLayout& cards) {
            auto in = cards.begin("Selected event");
            const bool stacked = in.getWidth() < 320;
            eventDepth.place(in, 150, stacked);
            eventSlice.place(in, 150, stacked);
            eventPriority.place(in, 150, stacked);
            auto flags = takeRow(in);
            const int half = flags.getWidth() / 2;
            eventEnabled.setBounds(flags.removeFromLeft(half));
            eventLocked.setBounds(flags);
            eventRestart.setBounds(takeRow(in));
            cards.end(in);
        };
        const auto placementCard = [this](CardLayout& cards) {
            auto in = cards.begin("Phrase placement and loop length");
            const bool stacked = in.getWidth() < 320;
            auto top = takeRow(in);
            enable.setBounds(top.removeFromLeft(top.getWidth() / 2));
            window.setBounds(top);
            start.place(in, 150, stacked);
            span.place(in, 150, stacked);
            every.place(in, 150, stacked);
            offset.place(in, 150, stacked);
            origin.place(in, 150, stacked);
            protect.place(in, 150, stacked);
            auto loop = takeRow(in);
            loopLane.setBounds(loop.removeFromLeft(stacked ? loop.getWidth() : 150));
            if (stacked)
                loop = takeRow(in);
            loopBeats.slider.setBounds(loop);
            cards.end(in);
        };
        if (wide)
        {
            CardLayout column(page, pad + mainW + gap, pad, rightColumnWidth, 1);
            eventCard(column);
            placementCard(column);
            contentBottom = std::max(contentBottom, column.bottom());
        }
        else
        {
            CardLayout row(page, pad, regionBottom + gap, mainW, 2);
            eventCard(row);
            placementCard(row);
            contentBottom = std::max(contentBottom, row.bottom());
        }
        return contentBottom + pad;
    }

private:
    static constexpr int wideBreakpoint = 1100;  // below this the event controls move under the lanes
    static constexpr int rightColumnWidth = 260;
    static constexpr int drawerWidth = 280;
    static constexpr int waveformHeight = 44;
    static constexpr int toolbarHeight = 32;   // one toolbar row, never taller

    int lastLocked = -1;
    bool loopShown = false;
    MenuToggleButton enable, modeButton, eventEnabled;
    MenuComboBox window, snap;
    juce::Label snapLabel, message, waveLabel;
    SliderRow start, span, every, offset, origin, protect, eventDepth, eventSlice, eventPriority, loopBeats;
    MenuToggleButton eventRestart, eventLocked;
    MenuComboBox loopLane;
    juce::TextButton convert, removeSelected, euclidToggle;
    LaneCanvas canvas;
    EuclidPanel euclid;
    WaveformView waveform; // same processor data as the Input tab, shown above the lanes
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
        distance.setup(page, "Distance", "How far the candidate moves from the stored scene: 0 = unchanged, 1 = full generated change. Enable flips need >= 0.5.", 0, 1, 0.01, 2);
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
        resultLabel.setJustificationType(juce::Justification::topLeft);
        history.setTextWhenNothingSelected("Generation history");
        history.setTooltip("Recent candidates saved on this computer. Selecting one regenerates it exactly "
                           "from its recorded source scene, seed, style, distance and candidate number.");
        history.onChange = [this] { recall(); };
        for (std::size_t i = 0; i < gm::operationCount; ++i)
        {
            // Seven toggles share one row: Sweep and Crush get the shorter Swp / Bit so nothing is ellipsised.
            static const char* const protectShort[] = {"Rpt", "Rev", "Gate", "Half", "Stop", "Swp", "Bit"};
            protectOp[i].setButtonText(protectShort[i]);
            protectOp[i].setTooltip(juce::String("Generator never changes ") + operationNames[static_cast<int>(i)] + " for this run.");
            add(protectOp[i]);
            protectOp[i].onClick = [this, i] {
                if (protectOp[i].getToggleState()) settings.protectedOperations |= 1u << i;
                else settings.protectedOperations &= ~(1u << i);
            };
        }
        setupLabel(protectLabel, "Never touch");
        setupLabel(locksTitle, "A locked field is protected from the generator, phrase evolution, edits, copies and MIDI while it is locked.", "A locked field cannot be changed by the generator, by phrase evolution, or by manual edits, whole-scene copies and MIDI controllers while it is locked: edits to it snap back. Locks do not change what you hear by themselves.");
        static const char* lockNames[] = {"Enable", "Depth", "Slice", "Events"};
        static const std::uint8_t lockBits[] = {ctl::lock::enable, ctl::lock::depth, ctl::lock::slice, ctl::lock::events};
        for (std::size_t op = 0; op < gm::operationCount; ++op)
        {
            setupLabel(lockRowLabel[op], theme::effectName(static_cast<int>(op)));
            add(lockRowLabel[op]);
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
                add(b);
            }
        }
        lockPlacement.setButtonText("Lock placement");
        lockPlacement.setTooltip("Generation and distance never change the phrase placement window, cadence or origin.");
        lockPlacement.onClick = [this] { edit([this](auto& s) { s.control.permissions.placementLocked = lockPlacement.getToggleState(); }); };
        loadPersistentHistory();
        page.extraPaint = [this](juce::Graphics& g) {
            for (std::size_t op = 0; op < gm::operationCount; ++op)
            {
                const auto colour = theme::effectColour(static_cast<int>(op));
                g.setColour(colour.withAlpha(0.2f));
                g.fillRoundedRectangle(chipRects[op].toFloat(), 4.0f);
                visual::drawGlyph(g, glyphEffect(static_cast<int>(op)), chipRects[op].toFloat().reduced(3.0f), colour, 1.8f);
            }
        };
        for (auto* c : std::initializer_list<juce::Component*>{&style, &styleLabel, &seedLabel, &seedEditor, &newSeed, &generate, &galleryGenerate, &generateEvents, &previous,
                                                               &regenerate, &candidateLabel, &resultLabel, &history, &protectLabel,
                                                               &locksTitle, &lockPlacement})
            add(*c);
        for (auto& button : galleryButtons)
            add(button);
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

protected:
    int layoutPage(int viewW, int) override
    {
        CardLayout cards(page, theme::space2, theme::space2, viewW - 2 * theme::space2, CardLayout::columnsFor(viewW));
        auto in = cards.begin("Seed and style");
        auto r = takeRow(in);
        styleLabel.setBounds(r.removeFromLeft(72));
        style.setBounds(r.removeFromLeft(std::min(r.getWidth(), 200)));
        r = takeRow(in);
        seedLabel.setBounds(r.removeFromLeft(72));
        newSeed.setBounds(r.removeFromRight(100));
        seedEditor.setBounds(r.reduced(4, 0));
        distance.place(in, 96);
        protectLabel.setBounds(takeRow(in, 18));
        {
            std::vector<std::pair<juce::Component*, int>> items;
            for (auto& b : protectOp)
                items.emplace_back(&b, 76);
            flowRow(in, items);
        }
        cards.end(in);

        in = cards.begin("Generate");
        flowRow(in, {{&generate, 160}, {&previous, 44}, {&regenerate, 130}, {&candidateLabel, 130}});
        galleryGenerate.setBounds(takeRow(in).removeFromLeft(std::min(in.getWidth(), 200)));
        gridRow(takeRow(in), {&galleryButtons[0], &galleryButtons[1], &galleryButtons[2]});
        generateEvents.setBounds(takeRow(in));
        resultLabel.setBounds(takeRow(in, 56));
        history.setBounds(takeRow(in));
        cards.end(in);

        in = cards.begin("Locks (protect fields)");
        locksTitle.setBounds(takeRow(in, std::max(18, wrappedHeight(locksTitle.getText(), locksTitle.getFont(), in.getWidth()))));
        for (std::size_t op = 0; op < gm::operationCount; ++op)
        {
            auto row = takeRow(in);
            chipRects[op] = row.removeFromLeft(26).withSizeKeepingCentre(22, 22);
            lockRowLabel[op].setBounds(row.removeFromLeft(96));
            gridRow(row, {&locks[op][0], &locks[op][1], &locks[op][2], &locks[op][3]});
        }
        lockPlacement.setBounds(takeRow(in));
        cards.end(in);
        return cards.bottom() + theme::space2;
    }

private:
    std::array<juce::Rectangle<int>, gm::operationCount> chipRects{};
    using Entry = gvar::HistoryEntry; // codec and tests: src/core/variation/GeneratorHistory.*

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
        const auto file = historyFile();
        if (!file.getParentDirectory().createDirectory().failed())
            file.replaceWithText(juce::String(gvar::serializeHistory(std::vector<Entry>(entries.begin(), entries.end()))));
    }

    void loadPersistentHistory()
    {
        const auto file = historyFile();
        if (!file.existsAsFile() || file.getSize() <= 0
            || file.getSize() > static_cast<juce::int64>(gvar::maxHistoryFileBytes))
            return;
        const auto parsed = gvar::parseHistory(file.loadFileAsString().toStdString());
        for (const auto& entry : parsed)
        {
            if (entries.size() >= gvar::maxHistoryEntries)
                break;
            entries.push_back(entry);
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
        info.setFont(theme::font(theme::fontBody));
        info.setColour(juce::Label::textColourId, theme::text);
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
                          + " were kept (Generate tab > Locks).";
            }
            message.setText(text, juce::dontSendNotification);
        };
        resetScene.setButtonText("Reset edited scene to factory");
        resetScene.setTooltip("Restores the factory name and settings of the edited scene. One undo step. Keeps the MIDI note. Disabled during a preview.");
        resetScene.onClick = [this] {
            const auto index = access.editingIndex();
            processor.resetVariation(index);
            const bool exact = processor.getSceneBank().variation(index) == gm::scene::SceneBank::defaultVariation(index);
            message.setText(exact ? "Scene reset (Undo available)." : "Scene reset, but locked fields were kept (Generate tab > Locks).",
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
            message.setText(exact ? "Effect reset." : "Effect not fully reset: locked fields were kept (Generate tab > Locks).",
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
                                                       : "Placement not fully cleared: a lock kept part of it (Generate tab > Locks).",
                            juce::dontSendNotification);
        };
        for (auto* c : std::initializer_list<juce::Component*>{&info, &target, &duplicate, &resetScene, &op, &resetOp, &clearPlacement, &message})
            add(*c);
        message.setColour(juce::Label::textColourId, theme::warning);
        message.setJustificationType(juce::Justification::topLeft);
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
protected:
    int layoutPage(int viewW, int) override
    {
        CardLayout cards(page, theme::space2, theme::space2, viewW - 2 * theme::space2, CardLayout::columnsFor(viewW));
        auto in = cards.begin("Edited scene");
        info.setBounds(takeRow(in, 56));
        auto r = takeRow(in);
        target.setBounds(r.removeFromLeft(80));
        duplicate.setBounds(r.reduced(theme::space1, 0));
        resetScene.setBounds(takeRow(in));
        cards.end(in);

        in = cards.begin("Effect and placement");
        r = takeRow(in);
        op.setBounds(r.removeFromLeft(std::min(180, r.getWidth() / 2)));
        resetOp.setBounds(r.reduced(theme::space1, 0));
        clearPlacement.setBounds(takeRow(in));
        message.setBounds(takeRow(in, 40));
        cards.end(in);
        return cards.bottom() + theme::space2;
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
        morphAmount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 26);
        morphAmount.setNumDecimalPlacesToDisplay(2);
        morphAmount.setTooltip("0 = sounding scene only, 1 = target scene. Effects present in only one scene fade in/out; shared effects interpolate depth and slice.");
        setupLabel(note, "Morph is applied on top of phrase placement of the sounding scene. It does not edit stored scenes.");
        note.setJustificationType(juce::Justification::topLeft);

        setupLabel(returnTitle, "Automatic return (host automatable)");
        setupLabel(returnBarsLabel, "Return after");
        returnBars.setSliderStyle(juce::Slider::LinearHorizontal);
        returnBars.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 26);
        returnBars.setNumDecimalPlacesToDisplay(0);
        returnBars.setTooltip("0 = off. After a scene switch, switch back after this many bars. Lands on the bar line. A new request, Cancel or a seek cancels it.");
        setupLabel(returnUnitLabel, "Unit");
        returnUnit.addItemList({"Bars", "Phrases"}, 1);
        returnUnit.setTooltip("Count the return delay in bars or in whole phrases (phrase length comes from the timing settings).");
        setupLabel(returnTargetLabel, "Return to");
        returnTarget.addItemList({"Previous scene", "A", "B", "C", "D", "Dry (no effects)"}, 1);
        returnTarget.setTooltip("Scene to return to. Previous returns to the scene that was sounding before the switch. Dry turns all effects off at the return point until you pick a scene.");
        setupLabel(returnStatus, "");

        for (auto* c : std::initializer_list<juce::Component*>{&morphTargetLabel, &morphTarget, &morphAmountLabel,
                                                               &morphAmount, &note, &returnBarsLabel, &returnBars,
                                                               &returnTargetLabel, &returnTarget, &returnStatus, &returnUnitLabel, &returnUnit})
            add(*c);
        auto& apvts = processor.parameters;
        attachments.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::morphTargetId, morphTarget));
        linkParam(morphTarget, GrooveMorphAudioProcessor::morphTargetId);
        attachments.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::morphAmountId, morphAmount));
        linkParam(morphAmount, GrooveMorphAudioProcessor::morphAmountId);
        morphAmount.textFromValueFunction = [](double v) { return juce::String(v, 2); }; // the attachment's own text shows 7 decimals
        morphAmount.updateText();
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
protected:
    int layoutPage(int viewW, int) override
    {
        CardLayout cards(page, theme::space2, theme::space2, viewW - 2 * theme::space2, CardLayout::columnsFor(viewW));
        auto in = cards.begin("Scene morph (host automatable)");
        auto r = takeRow(in);
        morphTargetLabel.setBounds(r.removeFromLeft(120));
        morphTarget.setBounds(r.removeFromLeft(std::min(r.getWidth(), 200)));
        r = takeRow(in);
        morphAmountLabel.setBounds(r.removeFromLeft(120));
        morphAmount.setBounds(r);
        note.setBounds(takeRow(in, std::max(18, wrappedHeight(note.getText(), note.getFont(), in.getWidth()))));
        cards.end(in);

        in = cards.begin("Automatic return (host automatable)");
        r = takeRow(in);
        returnBarsLabel.setBounds(r.removeFromLeft(120));
        returnBars.setBounds(r);
        r = takeRow(in);
        returnUnitLabel.setBounds(r.removeFromLeft(120));
        returnUnit.setBounds(r.removeFromLeft(std::min(r.getWidth(), 200)));
        r = takeRow(in);
        returnTargetLabel.setBounds(r.removeFromLeft(120));
        returnTarget.setBounds(r.removeFromLeft(std::min(r.getWidth(), 200)));
        returnStatus.setBounds(takeRow(in, 22));
        cards.end(in);
        return cards.bottom() + theme::space2;
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
        maxSlider.setup(page, "Maximum at once", "1-7 effects may be active together. Only enabled effects count.", 1, static_cast<double>(gm::operationCount), 1, 0);
        maxSlider.commit = [this] { edit([this](auto& s) { s.control.activity.maxConcurrent = static_cast<std::uint8_t>(maxSlider.slider.getValue()); }); };
        for (std::size_t i = 0; i < gm::operationCount; ++i)
        {
            prio[i].setup(page, operationNames[static_cast<int>(i)] + " priority", "0-100. Higher priority survives when the limit is exceeded; ties keep the earlier effect in chain order.", 0, 100, 1, 0);
            prio[i].commit = [this, i] { edit([this, i](auto& s) { s.control.activity.priority[i] = static_cast<std::uint8_t>(prio[i].slider.getValue()); }); };
        }
        spacing.setup(page, "Min event spacing (beats)", "Reduce action: event starts must be at least this far apart (0 = no limit).", 0, 4, 1.0 / 16.0, 3);
        spacing.commit = [this] { edit([this](auto& s) { s.control.activity.minSpacingBeats = static_cast<float>(spacing.slider.getValue()); }); };
        coverage.setup(page, "Max phrase coverage", "Reduce action: the union of event lengths may cover at most this fraction of the phrase.", 0, 1, 0.01, 2);
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
        regionStart.setup(page, "Region start (beats)", "Start of the selected protected region, in meter beats from the phrase start.", 0, 64, 1.0 / 16.0, 3);
        regionStart.commit = [this] { editRegion(true); };
        regionLength.setup(page, "Region length (beats)", "Length of the selected protected region, in meter beats.", 1.0 / 16.0, 64, 1.0 / 16.0, 3);
        regionLength.commit = [this] { editRegion(false); };
        scaleDouble.setButtonText("Events x2");
        scaleDouble.setTooltip("Doubles the position and length of every unlocked event (e.g. after doubling the phrase). Events that would fall past the phrase end are dropped. One undo step.");
        scaleDouble.onClick = [this] { scale(2, 1); };
        scaleHalf.setButtonText("Events /2");
        scaleHalf.setTooltip("Halves the position and length of every unlocked event. One undo step.");
        scaleHalf.onClick = [this] { scale(1, 2); };
        scaleResult.setColour(juce::Label::textColourId, theme::warning);
        setupLabel(note, "'Effects at once' is applied live. Spacing and coverage are explicit actions: they edit the event list only when you press Reduce (Undo restores).");
        note.setJustificationType(juce::Justification::topLeft);
        result.setColour(juce::Label::textColourId, theme::warning);
        result.setJustificationType(juce::Justification::topLeft);
        for (auto* c : std::initializer_list<juce::Component*>{&regionSelect, &regionAdd, &regionRemove, &scaleDouble, &scaleHalf, &scaleResult})
            add(*c);
        add(enable);
        add(note);
        add(analyze);
        add(reduce);
        add(result);
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
protected:
    int layoutPage(int viewW, int) override
    {
        CardLayout cards(page, theme::space2, theme::space2, viewW - 2 * theme::space2, CardLayout::columnsFor(viewW));
        auto in = cards.begin("Effects at once");
        enable.setBounds(takeRow(in));
        maxSlider.place(in, 150, in.getWidth() < 300);
        note.setBounds(takeRow(in, std::max(18, wrappedHeight(note.getText(), note.getFont(), in.getWidth()))));
        cards.end(in);

        in = cards.begin("Effect priority");
        for (auto& p : prio)
            p.place(in, 180, in.getWidth() < 300);
        cards.end(in);

        in = cards.begin("Reduce and rescale events");
        spacing.place(in, 180, in.getWidth() < 300);
        coverage.place(in, 180, in.getWidth() < 300);
        flowRow(in, {{&analyze, 110}, {&reduce, 140}, {&scaleDouble, 110}, {&scaleHalf, 110}});
        result.setBounds(takeRow(in, 40));
        scaleResult.setBounds(takeRow(in, 22));
        cards.end(in);

        in = cards.begin("Protected regions");
        flowRow(in, {{&regionSelect, 130}, {&regionAdd, 130}, {&regionRemove, 140}});
        regionStart.place(in, 180, in.getWidth() < 300);
        regionLength.place(in, 180, in.getWidth() < 300);
        cards.end(in);
        return cards.bottom() + theme::space2;
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
        loopBpmField.setTextToShowWhenEmpty("BPM", theme::textDim);
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
            {
                const auto busy = processor.captureConflict(GrooveMorphAudioProcessor::CaptureKind::LoopCapture);
                message.setText(busy.isNotEmpty() ? busy : juce::String("Capture could not start."), juce::dontSendNotification);
            }
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
        message.setColour(juce::Label::textColourId, theme::warning);

        setupLabel(bandTitle, "Band split (effects on part of the spectrum)");
        setupLabel(bandModeLabel, "Process");
        setupLabel(bandTopologyLabel, "Crossover type");
        bandMode.addItemList({"Full band", "Protect lows", "Lows only"}, 1);
        bandMode.setTooltip("Full band: unchanged. Protect lows: bass below the crossover bypasses the effects (keeps the low end solid). Lows only: only the bass goes through the effects. The slope is selectable: 6 dB/oct is gentle, 12 dB/oct is steeper.");
        bandSlope.addItemList({"6 dB/oct", "12 dB/oct"}, 1);
        bandSlope.setTooltip("Slope of the crossover filter around the effected band. 12 dB/oct rejects the other band more strongly; "
                             "the bypassed band is always the exact complement, so nothing is lost when the effects are off.");
        add(bandSlope);
        bandTopology.addItemList({"Classic", "Linkwitz-Riley 12", "Linkwitz-Riley 24"}, 1);
        bandTopology.setTooltip("Crossover type. Classic: the original filters (the slope menu applies). Linkwitz-Riley 12 or 24 dB/oct: the low and high parts sum back to a flat level; the slope menu is ignored.");
        add(bandTopology);
        setupLabel(crossoverLabel, "Crossover");
        crossover.setSliderStyle(juce::Slider::LinearHorizontal);
        crossover.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 26);
        crossover.setTextValueSuffix(" Hz");
        crossover.setTooltip("Crossover frequency, 40-400 Hz.");

        setupLabel(triggerTitle, "Audio trigger (F07)");
        setupLabel(triggerLabel, "On transient");
        trigger.addItemList({"Off", "Next scene", "Trigger queued"}, 1);
        trigger.setTooltip("Off, switch to the next scene (using the Transition setting) or fire a queued Manual transition whenever a drum-like transient is detected in the signal entering the effects.");
        setupLabel(sensLabel, "Sensitivity");
        sens.setSliderStyle(juce::Slider::LinearHorizontal);
        sens.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 26);
        sens.setNumDecimalPlacesToDisplay(2);
        sens.setTooltip("Higher reacts to softer transients (and false triggers).");
        setupLabel(onsets, "");

        midiMacros.setButtonText("MIDI macros: CC 20/21/22 and program change");
        midiMacros.setTooltip("CC 20 = Intensity, CC 21 = Dry/Wet, CC 22 = Morph amount; program change 0-3 = scene A-D. Applied at about 20 Hz via the host parameters. Existing explicit CC links override their macro slot. CC 20-22 cannot be learned or newly linked while macros are on. Off by default; not saved.");
        midiMacros.onClick = [this] { processor.setMidiMacrosEnabled(midiMacros.getToggleState()); };

        for (auto* c : std::initializer_list<juce::Component*>{&source, &bars, &loopBpmField, &capture, &importButton, &clearLoop, &matchTempo, &loopInfo, &stretchToggle, &stretchInfo, &waveform,
                                                               &message, &bandModeLabel, &bandTopologyLabel, &bandMode, &crossoverLabel, &crossover,
                                                               &triggerLabel, &trigger, &sensLabel, &sens, &onsets, &midiMacros})
            add(*c);
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
        sens.textFromValueFunction = [](double v) { return juce::String(v, 2); };
        sens.updateText();
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
protected:
    int layoutPage(int viewW, int) override
    {
        CardLayout cards(page, theme::space2, theme::space2, viewW - 2 * theme::space2, CardLayout::columnsFor(viewW));
        auto in = cards.begin("Loop", true);
        loopInfo.setBounds(takeRow(in, 24));
        waveform.setBounds(takeRow(in, 72));
        stretchInfo.setBounds(takeRow(in, 24));
        cards.end(in);

        in = cards.begin("Input source and loop");
        source.setBounds(takeRow(in).removeFromLeft(std::min(in.getWidth(), 280)));
        flowRow(in, {{&bars, 130}, {&loopBpmField, 90}, {&capture, 280}});
        flowRow(in, {{&importButton, 150}, {&clearLoop, 120}, {&matchTempo, 180}});
        stretchToggle.setBounds(takeRow(in));
        message.setBounds(takeRow(in, 40));
        cards.end(in);

        in = cards.begin("Band split (effects on part of the spectrum)");
        flowRow(in, {{&bandModeLabel, 80}, {&bandMode, 170}, {&bandSlope, 130}});
        flowRow(in, {{&bandTopologyLabel, 110}, {&bandTopology, 220}});
        auto r = takeRow(in);
        crossoverLabel.setBounds(r.removeFromLeft(100));
        crossover.setBounds(r);
        cards.end(in);

        in = cards.begin("Audio trigger and MIDI macros");
        r = takeRow(in);
        triggerLabel.setBounds(r.removeFromLeft(100));
        trigger.setBounds(r.removeFromLeft(std::min(r.getWidth(), 200)));
        r = takeRow(in);
        sensLabel.setBounds(r.removeFromLeft(100));
        sens.setBounds(r);
        onsets.setBounds(takeRow(in, 22));
        midiMacros.setBounds(takeRow(in));
        cards.end(in);
        return cards.bottom() + theme::space2;
    }
private:
    juce::Label bandTopologyLabel;
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
            add(sceneButtons[i]);
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
            macros[i].setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 26);
            macros[i].setNumDecimalPlacesToDisplay(2);
            add(macroLabels[i]);
            add(macros[i]);
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
        every.setup(page, "Every N phrases", "How often the evolving scene changes.", 1, 16, 1, 0);
        every.slider.setValue(2, juce::dontSendNotification);
        every.commit = [this] { applyEvolution(); };
        distance.setup(page, "Distance", "How far each step moves from the stored scene.", 0, 1, 0.01, 2);
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
        phaseOffset.setup(page, "Phase offset (beats)", "Added to the pattern start, -4 to +4 meter beats (step 1/16).", -4.0, 4.0, 1.0 / 16.0, 3);
        phaseOffset.commit = [this] {
            edit([this](auto& s) { s.control.phase.offsetBeats = static_cast<float>(phaseOffset.slider.getValue()); });
        };
        phaseSmooth.setup(page, "Transition smoothing (ms)", "Length of the cross-fade when the pattern jumps as this scene launches. 0 = default (3 ms), up to 250 ms.", 0.0, 250.0, 1.0, 0);
        phaseSmooth.commit = [this] {
            edit([this](auto& s) { s.control.phase.smoothingMs = static_cast<float>(phaseSmooth.slider.getValue()); });
        };
        restartScene.setButtonText("Restart scene now");
        restartScene.setTooltip("Restarts the active scene's pattern now. The transport and the active scene do not change.");
        restartScene.onClick = [this] { processor.restartActiveScene(); };
        setupLabel(phaseInfo, "");
        for (auto* c : std::initializer_list<juce::Component*>{&phasePolicy, &restartScene, &phaseInfo})
            add(*c);

        for (auto* c : std::initializer_list<juce::Component*>{&transitionLabel, &transition, &trigger, &cancel, &morphLabel, &morphTarget,
                                                               &gestureTarget, &record, &play, &clearGesture, &gestureInfo,
                                                               &evolveOn, &evolveScene, &evolveStyle, &evolveInfo})
            add(*c);
        auto& apvts = processor.parameters;
        combos.push_back(std::make_unique<ComboAttachment>(apvts, GrooveMorphAudioProcessor::transitionModeId, transition));
        linkParam(transition, GrooveMorphAudioProcessor::transitionModeId);
        sliders.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::masterIntensityId, macros[0]));
        linkParam(macros[0], GrooveMorphAudioProcessor::masterIntensityId);
        macros[0].textFromValueFunction = [](double v) { return juce::String(v, 2); };
        macros[0].updateText();
        sliders.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::dryWetId, macros[1]));
        linkParam(macros[1], GrooveMorphAudioProcessor::dryWetId);
        macros[1].textFromValueFunction = [](double v) { return juce::String(v, 2); };
        macros[1].updateText();
        sliders.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::morphAmountId, macros[2]));
        linkParam(macros[2], GrooveMorphAudioProcessor::morphAmountId);
        sliders.push_back(std::make_unique<SliderAttachment>(apvts, GrooveMorphAudioProcessor::chaosId, macros[3]));
        linkParam(macros[3], GrooveMorphAudioProcessor::chaosId);
        macros[2].textFromValueFunction = [](double v) { return juce::String(v, 2); };
        macros[2].updateText();
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
                                      i == active ? theme::sounding
                                                  : static_cast<int>(i) == queued ? theme::queued : theme::surfaceRaised);
        for (std::size_t i = 0; i < sceneButtons.size(); ++i)
            sceneButtons[i].setColour(juce::TextButton::textColourOffId,
                                      i == active || static_cast<int>(i) == queued ? theme::textOnAccent : theme::text);
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
protected:
    int layoutPage(int viewW, int) override
    {
        CardLayout cards(page, theme::space2, theme::space2, viewW - 2 * theme::space2, CardLayout::columnsFor(viewW));
        auto in = cards.begin("Scene launch");
        gridRow(takeRow(in, theme::padTarget), {&sceneButtons[0], &sceneButtons[1], &sceneButtons[2], &sceneButtons[3]});
        auto r = takeRow(in);
        transitionLabel.setBounds(r.removeFromLeft(90));
        transition.setBounds(r.removeFromLeft(std::min(r.getWidth(), 180)));
        flowRow(in, {{&trigger, 150}, {&cancel, 150}});
        cards.end(in);

        in = cards.begin("Macros");
        for (std::size_t i = 0; i < macros.size(); ++i)
        {
            r = takeRow(in);
            macroLabels[i].setBounds(r.removeFromLeft(110));
            macros[i].setBounds(r);
        }
        r = takeRow(in);
        morphLabel.setBounds(r.removeFromLeft(110));
        morphTarget.setBounds(r.removeFromLeft(std::min(r.getWidth(), 150)));
        cards.end(in);

        in = cards.begin("Gesture lane (records one phrase of movement)");
        gestureTarget.setBounds(takeRow(in).removeFromLeft(std::min(in.getWidth(), 200)));
        flowRow(in, {{&record, 100}, {&play, 100}, {&clearGesture, 100}});
        gestureInfo.setBounds(takeRow(in, 22));
        cards.end(in);

        in = cards.begin("Phrase evolution (sounding only, never edits scenes)");
        evolveOn.setBounds(takeRow(in));
        flowRow(in, {{&evolveScene, 90}, {&evolveStyle, 170}});
        every.place(in, 130, in.getWidth() < 300);
        distance.place(in, 130, in.getWidth() < 300);
        evolveInfo.setBounds(takeRow(in, 22));
        cards.end(in);

        in = cards.begin("Scene start phase (edited scene)");
        flowRow(in, {{&phasePolicy, 200}, {&restartScene, 170}});
        phaseOffset.place(in, 160, in.getWidth() < 300);
        phaseSmooth.place(in, 160, in.getWidth() < 300);
        phaseInfo.setBounds(takeRow(in, 22));
        cards.end(in);
        return cards.bottom() + theme::space2;
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
// Info tab: honest feature list
// ---------------------------------------------------------------------------------------
class StatusTab final : public juce::Component
{
public:
    StatusTab()
    {
        text.setMultiLine(true);
        text.setReadOnly(true);
        text.setCaretVisible(false);
        text.setScrollbarsShown(true);
        text.setFont(theme::font(theme::fontBody));
        text.setColour(juce::TextEditor::backgroundColourId, theme::surface);
        text.setColour(juce::TextEditor::textColourId, theme::text);
        text.setColour(juce::TextEditor::outlineColourId, theme::stroke);
        text.setColour(juce::TextEditor::focusedOutlineColourId, theme::focusRing);
        text.setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
        text.setTooltip("Honest list of what this beta implements and what it does not (read only).");
        text.setTitle("Feature status");
        text.setWantsKeyboardFocus(true);
        text.setText(
            "BETA FEATURE STATUS (the docs folder has the full feature status and known issues)\n\n"
            "Implemented (automated tests; not tried in a DAW or by ear):\n"
            " - Seven effects: Repeat, Reverse, Gate, Half-time, Tape stop, Filter sweep, Bit crush\n"
            " - Pattern tab: placement window/cadence/origin/snap, downbeat protect, up to 8 protected regions,\n"
            "   event lanes (up to 256 events) with per-event depth, slice, priority, cycle restart and lock;\n"
            "   per-lane loop lengths; Euclid generator and per-event / per-lane chance\n"
            " - Generate tab: seed, style, distance, three-option gallery, event-pattern generation, locks,\n"
            "   preview/apply/revert, locally saved 24-entry history\n"
            " - Scenes: duplicate, reset, copy FX, rename, undo/redo, morph, auto-return (bars/phrases),\n"
            "   scene start phase, per-scene transition smoothing (follow / restart / snap to next beat, offset, Restart scene now)\n"
            " - Limits: effects at once + priority; spacing/coverage via Reduce; Events x2 and /2\n"
            " - Input: demo loop, capture/import loop (beat-locked, optional stretch to host tempo, saved in the\n"
            "   project), band split (Classic 6/12 dB or Linkwitz-Riley 12/24 dB), audio trigger,\n"
            "   MIDI CC/program-change macros (tick MIDI macros first)\n"
            " - Perform: scene launch, macros, gesture lane, phrase evolution, Chaos macro\n"
            " - Presets: factory and user presets in the preset bar, host programs, dry return target\n"
            " - Easy mode with first-run guide; Surprise me; Visual window\n"
            " - Print: record the processed output for N bars to a 24-bit WAV, drag it into the DAW (untested)\n"
            " - Right-click on any control: exact value, copy/paste, reset, MIDI learn\n"
            " - Formats: VST3, Standalone and CLAP (CLAP is built in CI and has not been loaded in a CLAP host)\n"
            " - Host Variation parameter follows the sounding scene\n\n"
            "Not implemented / limited in this beta:\n"
            " - Loop stretch is WSOLA only (ratio 0.5 to 2.0); saved loop audio is 16-bit\n"
            " - Generator history is saved locally on this computer, not embedded in the DAW project\n"
            "   (gesture curve, evolution settings and MIDI links are); gestures run at ~20 Hz\n"
            " - Capture is stereo-limited, 20 s max; only two crossover bands; AU, AAX and LV2 are not planned\n\n"
            "Untested: real DAW hosts, audio hardware, listening quality.",
            false);
        addAndMakeVisible(text);
    }
    void paint(juce::Graphics& g) override { g.fillAll(theme::bg); }
    void resized() override { text.setBounds(getLocalBounds().reduced(theme::space2)); }
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
        // Nine tabs; the order is the public tab index (showInspectorTab): 0 Pattern, 1 Generate, 2 Scenes, 3 Morph,
        // 4 Limits, 5 Input, 6 Perform, 7 Info, 8 Print. The tooltips carry the longer descriptions.
        const char* names[] = {"Pattern", "Generate", "Scenes", "Morph", "Limits", "Input", "Perform", "Info", "Print"};
        const char* full[] = {"Pattern: choose where in the phrase effects act (window, every N phrases), draw event lanes and use the Euclid / chance drawer",
                              "Generate: generate variations of a scene (seed, style, distance) and lock parts of it",
                              "Scenes: duplicate, copy, reset and clear scenes and effects",
                              "Morph: blend two scenes, and automatically return to a scene after a few bars or phrases",
                              "Limits: cap how many effects play at once and how busy a scene may get",
                              "Input: choose the input source, capture or import a loop, split the bass, audio triggers, MIDI macros",
                              "Perform: live macros, record and play back a gesture, evolve a scene over phrases",
                              "Info: what this beta version implements and what is untested",
                              "Print: record the processed output for N bars to a WAV file, drag it into your DAW or reuse it as the loop source"};
        juce::Component* contents[] = {placement.get(), generator.get(), scenes.get(), morph.get(), limits.get(), input.get(),
                                       perform.get(), status.get(), print.get()};
        tabs.setTabBarDepth(32);
        tabs.setOutline(0);
        tabs.setIndent(0);
        tabs.setColour(juce::TabbedComponent::backgroundColourId, theme::bg);
        tabs.setColour(juce::TabbedComponent::outlineColourId, theme::stroke);
        for (int i = 0; i < 9; ++i)
        {
            tabs.addTab(names[i], theme::bg, contents[i], false);
            if (auto* button = tabs.getTabbedButtonBar().getTabButton(i))
            {
                button->setTooltip(full[i]);
                button->setDescription(full[i]);
                button->setWantsKeyboardFocus(true); // the tab strip is the first stop of the keyboard focus order
            }
        }
        for (auto* tab : std::initializer_list<InspectorTab*>{placement.get(), generator.get(), scenes.get(), morph.get(),
                                                              limits.get(), input.get(), perform.get()})
            tab->polishControls();
        polish(*status);
        polish(*print);
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

void InspectorPanel::paint(juce::Graphics& g) { g.fillAll(theme::bg); }

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
