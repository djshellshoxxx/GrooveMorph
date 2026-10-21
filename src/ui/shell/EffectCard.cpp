#include "ui/shell/EffectCard.h"

#include "PluginProcessor.h"
#include "ui/shell/ShellUtil.h"
#include "ui/visual/Glyphs.h"

#include <iterator>

namespace groovemorph::ui::shell
{
namespace theme = groovemorph::ui::theme;

EffectCard::EffectCard(GrooveMorphAudioProcessor& processor, std::size_t effectIndex)
    : enabled(theme::effectName(static_cast<int>(effectIndex))), index(effectIndex)
{
    static const char* const longNames[] = {"Repeat / stutter", "Reverse slice", "Gate", "Half-time", "Tape stop", "Filter sweep", "Bit crush"};
    static const char* const effectInfo[] = {
        "Repeat / stutter: repeats a short slice of the sound again and again.",
        "Reverse slice: plays the previous slice backwards.",
        "Gate: chops the sound on and off in time (open for the first half of each slice, closed for the second half).",
        "Half-time: plays each slice at half speed for a slowed, heavier feel (pitch drops with it).",
        "Tape stop: slows each slice down to a stop, like a turntable or tape machine losing power.",
        "Filter sweep: a resonant low-pass filter sweeps from bright to dark across each slice and back open on the next one (DJ-style).",
        "Bit crush: gets grittier across each slice (lower sample rate and fewer bits), then resets at the next slice."};
    static_assert(std::size(effectInfo) == groovemorph::core::operationCount, "one tooltip per operation");
    static_assert(std::size(longNames) == groovemorph::core::operationCount, "one name per operation");
    const juce::String name = longNames[effectIndex];
    const juce::String field = juce::String(static_cast<int>(effectIndex));

    configureLinear(depth, name + " depth", 38, 18);
    configureLinear(slice, name + " slice (meter beats)", 44, 18);
    enabled.info() = {&processor, name + " on/off (sounding scene)", "s:" + field + ":enabled", {}};
    depth.info() = {&processor, name + " depth (sounding scene)", "s:" + field + ":depth", {}};
    slice.info() = {&processor, name + " slice (sounding scene)", "s:" + field + ":slice", "beats"};
    depth.setDoubleClickReturnValue(true, 1.0);
    slice.setDoubleClickReturnValue(true, 0.25);
    depth.setRange(0, 1, 0.01);
    depth.setNumDecimalPlacesToDisplay(2);
    slice.setNumDecimalPlacesToDisplay(3);
    slice.setRange(1.0 / 16.0, 4, 1.0 / 16.0);
    describe(enabled, name + " on/off", juce::String(effectInfo[effectIndex]) + " Tick to switch it on for the scene being edited.");
    describe(depth, name + " depth",
             name + " depth: how much of the effect is mixed in for the scene being edited (0 = none, 1 = full).");
    describe(slice, name + " slice",
             name + " slice in meter beats. Bounded 4-second history limits effective duration: repeat < 1.33 s, reverse < 2 s, half-time/tape stop < 4 s; gate, filter sweep and bit crush use the full requested value.");

    enabled.onClick = [this] { repaint(); if (onEdit) onEdit(index, 0); };
    depth.onDragStart = [this] { draggingDepth = true; };
    slice.onDragStart = [this] { draggingSlice = true; };
    depth.onDragEnd = [this] { draggingDepth = false; if (onEdit) onEdit(index, 1); };
    slice.onDragEnd = [this] { draggingSlice = false; if (onEdit) onEdit(index, 2); };
    depth.onValueChange = [this] { if (!draggingDepth && onEdit) onEdit(index, 1); };
    slice.onValueChange = [this] { if (!draggingSlice && onEdit) onEdit(index, 2); };
    for (auto* c : std::initializer_list<juce::Component*>{&enabled, &depth, &slice})
    {
        c->setWantsKeyboardFocus(true);
        addAndMakeVisible(*c);
    }
}

EffectCard::~EffectCard() = default;

void EffectCard::setOperation(bool isEnabled, float depthValue, float sliceBeats)
{
    enabled.setToggleState(isEnabled, juce::dontSendNotification);
    depth.setValue(depthValue, juce::dontSendNotification);
    slice.setValue(sliceBeats, juce::dontSendNotification);
    repaint();
}

void EffectCard::paint(juce::Graphics& g)
{
    const auto colour = theme::effectColour(static_cast<int>(index));
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme::surfaceRaised);
    g.fillRoundedRectangle(r, theme::radius);
    g.setColour(enabled.getToggleState() ? colour : theme::stroke);
    g.drawRoundedRectangle(r, theme::radius, enabled.getToggleState() ? 1.5f : 1.0f);
    // Colour bar along the top edge, glyph beside the name: colour is never the only cue.
    g.setColour(colour);
    g.fillRoundedRectangle(r.getX() + 1.0f, r.getY() + 1.0f, r.getWidth() - 2.0f, 3.0f, 1.5f);
    groovemorph::ui::visual::drawGlyph(g, static_cast<groovemorph::core::viz::Effect>(index + 1), glyphArea.toFloat(), colour, 1.8f);
    paintCaption(g, "Depth", depthCaption);
    paintCaption(g, "Slice", sliceCaption);
}

void EffectCard::resized()
{
    auto area = getLocalBounds().reduced(theme::space1 + 1, 0);
    area.removeFromTop(4);
    const int rowHeight = 18;
    auto top = area.removeFromTop(rowHeight);
    glyphArea = top.removeFromLeft(16).withSizeKeepingCentre(16, 16);
    top.removeFromLeft(2);
    enabled.setBounds(top);
    area.removeFromTop(2);
    const int captionWidth = 32;
    auto depthRow = area.removeFromTop(rowHeight);
    depthCaption = depthRow.removeFromLeft(captionWidth);
    depth.setBounds(depthRow);
    area.removeFromTop(2);
    auto sliceRow = area.removeFromTop(rowHeight);
    sliceCaption = sliceRow.removeFromLeft(captionWidth);
    slice.setBounds(sliceRow);
}
} // namespace groovemorph::ui::shell
