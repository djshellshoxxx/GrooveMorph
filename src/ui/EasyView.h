#pragma once

#include <JuceHeader.h>

#include "ui/ControlMenu.h"
#include "ui/FirstRunGuide.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

class GrooveMorphAudioProcessor;

namespace groovemorph::ui
{
// N05 Easy mode: one compact screen (four scene buttons, Intensity / Dry-wet / Morph / Chaos, Surprise me,
// Undo last change, Reset / Panic, plain-language timing line, link to the full inspector). It fills the
// whole editor, so it scales like the editor; geometry comes from core/easy/EasyLayout.h (unit tested).
// Behavior and rationale: docs/specs/N05-EASY-MODE.md section 4.
class EasyView final : public juce::Component
{
public:
    explicit EasyView(GrooveMorphAudioProcessor& processor);
    ~EasyView() override;

    // Asked to switch to the full editor (the owner hides this view).
    std::function<void()> onOpenFullView;

    // Re-read processor state (called from the editor timer, about 10 Hz, only while visible).
    void refresh();
    // Opens the first-run guide again ("Show guide").
    void showGuide();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    struct EasyLookAndFeel;

    void addMacro(std::size_t index, const char* parameter, const juce::String& label, const juce::String& help);
    void setMessage(const juce::String& text);
    void onSceneClicked(std::size_t index);
    void onSurprise();
    void onUndo();
    void onPanic();
    void retargetMorph();

    GrooveMorphAudioProcessor& processor;
    std::unique_ptr<EasyLookAndFeel> lookAndFeel;
    juce::Label titleLabel, timingLabel, messageLabel;
    std::array<juce::Label, 4> macroLabels;
    std::array<MenuSlider, 4> sliders;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;
    std::array<juce::TextButton, 4> sceneButtons;
    juce::TextButton surpriseButton{"Surprise me"}, undoButton{"Undo last change"}, panicButton{"Reset / Panic"},
                     guideButton{"Show guide"}, fullViewButton{"Open full view"};
    std::unique_ptr<FirstRunGuide> guide;
    juce::uint32 messageUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EasyView)
};
} // namespace groovemorph::ui
