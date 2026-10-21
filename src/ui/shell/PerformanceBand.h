#pragma once

#include <JuceHeader.h>

#include "ui/ControlMenu.h"
#include "ui/shell/ScenePad.h"

#include <array>
#include <memory>
#include <vector>

class GrooveMorphAudioProcessor;

namespace groovemorph::ui::shell
{
// Scene pads, transition and trigger controls, the four macro knobs (Chaos is the hero) and Surprise / Undo / Redo.
// Parameter-bound controls are attached here; the owner wires the button callbacks.
class PerformanceBand final : public juce::Component
{
public:
    static constexpr int height = 108;

    explicit PerformanceBand(GrooveMorphAudioProcessor& processor);
    ~PerformanceBand() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void tickPads() { for (auto& pad : pads) pad.tick(); }

    std::array<ScenePad, 4> pads;
    groovemorph::ui::MenuComboBox transitionMode, morphTarget;
    juce::TextButton triggerButton{"Trigger queued"}, cancelButton{"Cancel queued"};
    groovemorph::ui::MenuSlider intensityKnob, dryWetKnob, morphKnob, chaosKnob;
    juce::TextButton surpriseButton{"Surprise me"}, undoButton{"Undo"}, redoButton{"Redo"};

private:
    std::array<juce::Rectangle<int>, 4> captionBounds; // Intensity, Dry / wet, Morph, Chaos (set in resized)
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerformanceBand)
};
} // namespace groovemorph::ui::shell
