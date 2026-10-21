#include "ui/shell/PerformanceBand.h"

#include "PluginProcessor.h"
#include "ui/shell/ShellUtil.h"

#include <algorithm>

namespace groovemorph::ui::shell
{
namespace theme = groovemorph::ui::theme;

PerformanceBand::PerformanceBand(GrooveMorphAudioProcessor& processor)
{
    for (std::size_t i = 0; i < pads.size(); ++i)
    {
        pads[i].setIndex(static_cast<int>(i));
        describe(pads[i], "Scene " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)),
                 "Switch to this scene (A-D). It takes effect according to the Transition setting: now, next beat, bar, phrase, or when you press Trigger queued. Shows which scene is sounding, queued and being edited. Shortcut: keys 1-4.");
        addAndMakeVisible(pads[i]);
    }

    bindCombo(processor, *this, transitionMode, comboAttachments, "transitionMode",
              {"Immediate", "Next Beat", "Next Bar", "Next Phrase", "Manual"}, "Transition");
    transitionMode.setTooltip("When a scene change takes effect: Immediate, on the Next Beat, Next Bar, Next Phrase, or Manual (waits until you press Trigger queued).");
    bindCombo(processor, *this, morphTarget, comboAttachments, "morphTarget", {"Off", "A", "B", "C", "D"}, "Morph target");
    morphTarget.setTooltip("Which scene the Morph knob blends the sounding scene toward. Off means the Morph knob has no effect.");

    describe(triggerButton, "Trigger queued",
             "Fires the queued scene change now. It only affects changes that were queued while Transition was set to Manual.");
    describe(cancelButton, "Cancel queued", "Cancels the queued scene change so the current scene keeps sounding.");

    const auto knob = [&](groovemorph::ui::MenuSlider& slider, const char* parameter, const juce::String& caption,
                          const juce::String& menuName, const juce::String& tip) {
        configureKnob(slider, caption);
        bindSlider(processor, *this, slider, sliderAttachments, parameter, menuName);
        slider.textFromValueFunction = [](double value) { return juce::String(value, 2); };
        slider.setNumDecimalPlacesToDisplay(2);
        describe(slider, menuName, tip);
        slider.setWantsKeyboardFocus(true);
    };
    knob(intensityKnob, "masterIntensity", "Intensity", "Intensity (0 = dry)",
         "How strongly the rhythm effects are applied: 0 = none (you hear the unprocessed sound), 1 = full. Same control as the Intensity macro in the Perform tab.");
    knob(dryWetKnob, "dryWet", "Dry / wet", "Dry / wet",
         "Mix between the unprocessed sound (0) and the processed sound (1). Intensity and Dry/wet multiply, so either one at 0 gives the dry sound.");
    knob(morphKnob, "morphAmount", "Morph", "Morph amount",
         "Blends the sounding scene toward the Morph target scene. 0 leaves the scene as it is.");
    knob(chaosKnob, "chaos", "Chaos", "Chaos",
         "Makes the sounding scene busier: more effects, deeper and shorter slices. 0 leaves it exactly as stored. Your saved scenes are never changed.");
    chaosKnob.getProperties().set("hero", true);

    describe(surpriseButton, "Surprise me",
             "Tries a new variation of the sounding scene and plays it now. It is kept at the next phrase. Press again for another one.");
    surpriseButton.setDescription("Preview a new variation of the sounding scene and keep it at the next phrase");
    describe(undoButton, "Undo", "Takes back the pending Surprise, or otherwise undoes the last scene edit (effects, names, MIDI notes, generated variations). Shortcut: Ctrl/Cmd+Z.");
    describe(redoButton, "Redo", "Redoes the scene edit you just undid. Shortcut: Ctrl/Cmd+Shift+Z or Ctrl+Y.");
    for (auto* c : std::initializer_list<juce::Component*>{&triggerButton, &cancelButton, &surpriseButton, &undoButton, &redoButton})
    {
        c->setWantsKeyboardFocus(true);
        addAndMakeVisible(*c);
    }
}

PerformanceBand::~PerformanceBand() = default;

void PerformanceBand::paint(juce::Graphics& g)
{
    paintPanel(g, getLocalBounds());
    // Knob captions (component name, 12 px). The value readout is drawn inside the knob by GrooveLookAndFeel.
    const std::array<const groovemorph::ui::MenuSlider*, 4> knobs{&intensityKnob, &dryWetKnob, &morphKnob, &chaosKnob};
    for (std::size_t i = 0; i < knobs.size(); ++i)
    {
        const bool hero = i == 3;
        g.setColour(hero ? theme::accent : theme::textDim);
        g.setFont(theme::font(theme::fontLabel, hero));
        g.drawFittedText(knobs[i]->getName(), captionBounds[i], i == 2 ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.7f);
    }
}

void PerformanceBand::resized()
{
    // Everything fits the fixed 108 px band: 8 px padding leaves 92 px of content height.
    auto area = getLocalBounds().reduced(theme::space2);
    const int gap = theme::space2;
    const int rightWidth = std::clamp(static_cast<int>(static_cast<float>(getWidth()) * 0.56f), 470, 780);
    auto right = area.removeFromRight(rightWidth);
    area.removeFromRight(theme::space3);

    // Left: four equal pads (56 high), then Transition / Trigger / Cancel (32 high).
    auto padRow = area.removeFromTop(theme::padTarget);
    const int padWidth = (padRow.getWidth() - 3 * gap) / 4;
    for (auto& pad : pads)
    {
        pad.setBounds(padRow.removeFromLeft(padWidth));
        padRow.removeFromLeft(gap);
    }
    area.removeFromTop(4);
    auto controlRow = area.removeFromTop(theme::minTarget);
    const int triggerWidth = std::clamp(controlRow.getWidth() / 4, 96, 130);
    cancelButton.setBounds(controlRow.removeFromRight(triggerWidth));
    controlRow.removeFromRight(gap);
    triggerButton.setBounds(controlRow.removeFromRight(triggerWidth));
    controlRow.removeFromRight(gap);
    transitionMode.setBounds(controlRow.removeFromLeft(std::min(controlRow.getWidth(), 170)));

    // Right: three knobs, the hero knob, then a button column (Surprise on top, Undo | Redo below).
    const int columnWidth = std::clamp(right.getWidth() / 5, 100, 130);
    auto column = right.removeFromRight(columnWidth);
    right.removeFromRight(gap);
    const int heroWidth = std::clamp(right.getWidth() / 4 + 12, 96, 150);
    auto heroCell = right.removeFromRight(heroWidth);
    right.removeFromRight(gap);
    const int knobWidth = (right.getWidth() - 2 * gap) / 3;

    // Each cell: knob on top, caption strip under it. Caption text always sits in the bottom 16 px so the
    // four captions share a baseline; the Morph cell widens its strip to 24 px to hold the target combo.
    constexpr int captionHeight = 16, morphStripHeight = 24;
    const auto knobCell = [&](juce::Slider& knob, std::size_t index, int strip) {
        auto cell = right.removeFromLeft(knobWidth);
        right.removeFromLeft(gap);
        auto strips = cell.removeFromBottom(strip);
        knob.setBounds(cell);
        captionBounds[index] = strips.removeFromBottom(captionHeight);
    };
    knobCell(intensityKnob, 0, morphStripHeight);
    knobCell(dryWetKnob, 1, morphStripHeight);
    auto morphCell = right.removeFromLeft(knobWidth);
    right.removeFromLeft(gap);
    auto morphStrip = morphCell.removeFromBottom(morphStripHeight);
    morphKnob.setBounds(morphCell);
    const int morphCaptionWidth = 42;
    captionBounds[2] = morphStrip.removeFromLeft(morphCaptionWidth).withSizeKeepingCentre(morphCaptionWidth, captionHeight);
    morphTarget.setBounds(morphStrip);

    auto heroStrip = heroCell.removeFromBottom(morphStripHeight - 2);
    chaosKnob.setBounds(heroCell);
    captionBounds[3] = heroStrip.removeFromBottom(captionHeight);

    surpriseButton.setBounds(column.removeFromTop(52));
    column.removeFromTop(gap);
    auto lower = column.removeFromTop(theme::minTarget);
    undoButton.setBounds(lower.removeFromLeft((lower.getWidth() - 4) / 2));
    lower.removeFromLeft(4);
    redoButton.setBounds(lower);
}
} // namespace groovemorph::ui::shell
