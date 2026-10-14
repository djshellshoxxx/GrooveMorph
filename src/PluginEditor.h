#pragma once

#include <JuceHeader.h>
#include "core/scene/SceneBank.h"
#include "ui/InspectorPanel.h"
#include "ui/visual/VisualWindow.h"

#include <memory>
#include <vector>

class GrooveMorphAudioProcessor;

class GrooveMorphAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit GrooveMorphAudioProcessorEditor(GrooveMorphAudioProcessor& processor);
    ~GrooveMorphAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void showInspectorTab(int index) { if (inspector != nullptr) inspector->showTab(index); }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    struct OperationRow
    {
        juce::ToggleButton enabled;
        juce::Slider depth, slice;
        bool draggingDepth = false, draggingSlice = false;
    };
    GrooveMorphAudioProcessor& audioProcessor;
    juce::TooltipWindow tooltipWindow{this, 700};
    juce::Label titleLabel, timingLabel, statusLabel, midiLabel;
    juce::Slider intensitySlider, dryWetSlider, tempoSlider, meterSlider, phraseSlider, midiNoteSlider;
    juce::ComboBox timingMode, transitionMode, denominator, editVariation;
    juce::ToggleButton bypassButton{"Bypass"}, returnOnNoteOff{"Return on MIDI note-off"},
                       demoButton{"Demo loop"};
    juce::ToggleButton visualButton{"Visual"};
    std::unique_ptr<groovemorph::ui::visual::VisualWindow> visualWindow;
    std::unique_ptr<groovemorph::ui::InspectorPanel> inspector;
    std::array<juce::TextButton, 4> variationButtons;
    std::array<OperationRow, groovemorph::core::operationCount> operations;
    juce::TextEditor variationName;
    juce::TextButton triggerButton{"Trigger queued"}, cancelButton{"Cancel queued"};
    juce::TextButton copyActiveFxButton{"Copy FX"};
    juce::TextButton previewButton{"Preview"}, compareButton{"Hear stored"},
                     applyButton{"Apply preview"}, revertButton{"Revert preview"},
                     undoButton{"Undo"}, redoButton{"Redo"};
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::size_t editingIndex = 0;
    groovemorph::core::scene::Variation editedVariation;
    bool refreshing = false, comparingStored = false, draggingMidi = false;
    void timerCallback() override;
    static constexpr int inspectorWidth = 470;
    int contentWidth() const noexcept { return getWidth() - inspectorWidth; }
    void applySceneSettings(groovemorph::core::VariationSettings settings);
    juce::uint32 lockNoticeUntil = 0;
    void previewCandidate(const groovemorph::core::VariationSettings& settings);
    void refreshSceneControls();
    void editOperation(std::size_t index, int field); // 0 enable, 1 depth, 2 slice
    void editName();
    void editMidiMap();
    void finishPreview(bool apply);
    void addSlider(juce::Slider&, const juce::String& parameter, const juce::String& name);
    void addCombo(juce::ComboBox&, const juce::String& parameter,
                  const juce::StringArray& choices, const juce::String& name);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMorphAudioProcessorEditor)
};
