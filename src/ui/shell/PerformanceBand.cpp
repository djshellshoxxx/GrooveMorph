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
}

void PerformanceBand::resized()
{
    auto area = getLocalBounds().reduced(theme::space2);
    const int gap = theme::space2;
    // Right zone (macros) takes about half the width, never less than what four knobs and the button column need.
    const int rightWidth = std::clamp(static_cast<int>(static_cast<float>(getWidth()) * 0.52f), 470, 760);
    auto right = area.removeFromRight(rightWidth);
    area.removeFromRight(theme::space3);

    // Left: four equal pads (64 high), then Transition / Trigger / Cancel.
    auto padRow = area.removeFromTop(64);
    const int padWidth = (padRow.getWidth() - 3 * gap) / 4;
    for (auto& pad : pads)
    {
        pad.setBounds(padRow.removeFromLeft(padWidth));
        padRow.removeFromLeft(gap);
    }
    area.removeFromTop(gap);
    auto controlRow = area.removeFromTop(theme::minTarget);
    const int triggerWidth = std::clamp(controlRow.getWidth() / 4, 96, 130);
    cancelButton.setBounds(controlRow.removeFromRight(triggerWidth));
    controlRow.removeFromRight(gap);
    triggerButton.setBounds(controlRow.removeFromRight(triggerWidth));
    controlRow.removeFromRight(gap);
    transitionMode.setBounds(controlRow.removeFromLeft(std::min(controlRow.getWidth(), 170)));

    // Right: three knobs, the hero knob, then a button column.
    const int columnWidth = std::clamp(right.getWidth() / 5, 100, 130);
    auto column = right.removeFromRight(columnWidth);
    right.removeFromRight(gap);
    const int heroWidth = std::clamp(right.getWidth() / 4 + 12, 96, 150);
    chaosKnob.setBounds(right.removeFromRight(heroWidth));
    right.removeFromRight(gap);
    const int knobWidth = (right.getWidth() - 2 * gap) / 3;
    const auto place = [&](juce::Slider& knob) {
        auto cell = right.removeFromLeft(knobWidth);
        right.removeFromLeft(gap);
        knob.setBounds(cell.removeFromTop(std::min(cell.getHeight() - theme::minTarget - 4, 84)));
        return cell;
    };
    (void) place(intensityKnob);
    (void) place(dryWetKnob);
    auto morphCell = place(morphKnob);
    morphTarget.setBounds(morphCell.removeFromBottom(theme::minTarget).withTrimmedLeft(4).withTrimmedRight(4));

    const int buttonHeight = (column.getHeight() - 2 * gap) / 3;
    surpriseButton.setBounds(column.removeFromTop(buttonHeight));
    column.removeFromTop(gap);
    undoButton.setBounds(column.removeFromTop(buttonHeight));
    column.removeFromTop(gap);
    redoButton.setBounds(column);
}
} // namespace groovemorph::ui::shell
