#pragma once

#include <JuceHeader.h>
#include "core/scene/SceneBank.h"
#include "ui/InspectorPanel.h"
#include "ui/ControlMenu.h"
#include "ui/PresetBar.h"
#include "ui/EasyView.h"
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
    bool keyPressed(const juce::KeyPress&) override;
    void showInspectorTab(int index) { if (inspector != nullptr) inspector->showTab(index); }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    struct OperationRow
    {
        groovemorph::ui::MenuToggleButton enabled;
        groovemorph::ui::MenuSlider depth, slice;
        bool draggingDepth = false, draggingSlice = false;
    };
    GrooveMorphAudioProcessor& audioProcessor;
    juce::TooltipWindow tooltipWindow{this, 700};
    juce::Label titleLabel, timingLabel, statusLabel, midiLabel;
    groovemorph::ui::MenuSlider intensitySlider, dryWetSlider, tempoSlider, meterSlider, phraseSlider, midiNoteSlider;
    groovemorph::ui::MenuComboBox timingMode, transitionMode, denominator, editVariation;
    groovemorph::ui::MenuToggleButton bypassButton{"Bypass"}, returnOnNoteOff{"Return on MIDI note-off"},
                       demoButton{"Demo loop"};
    groovemorph::ui::MenuToggleButton visualButton{"Visual"};
    std::unique_ptr<groovemorph::ui::visual::VisualWindow> visualWindow;
    std::unique_ptr<groovemorph::ui::InspectorPanel> inspector;
    groovemorph::ui::PresetBar presetBar{audioProcessor}; // N02
    // N05 Easy mode: a full-size view above the editor's own controls (hidden while the full view is shown).
    juce::TextButton easyModeButton{"Easy mode"};
    std::unique_ptr<groovemorph::ui::EasyView> easyView;
    std::vector<juce::Component::SafePointer<juce::Component>> hiddenByEasy;
    bool easyShown = false;
    void applyEasyMode(bool on);
    std::array<juce::TextButton, 4> variationButtons;
    std::array<OperationRow, groovemorph::core::operationCount> operations;
    juce::TextEditor variationName;
    juce::TextButton playButton{"Play"}, stopButton{"Stop"}, resetButton{"Reset"};
    struct TipArea final : juce::Component, juce::SettableTooltipClient {};
    TipArea phraseStripTip;
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
    bool midiSwapDialogOpen = false;     // A04: a note-swap confirmation is showing
    juce::uint32 midiNoticeUntil = 0;    // A04: "MIDI link table full" shown in the status line until then
    juce::uint32 midiEditDue = 0;        // A04: keyboard/wheel note steps are applied after a short pause (0 = none)
    void timerCallback() override;
    static constexpr int inspectorWidth = 470;
    int contentWidth() const noexcept { return getWidth() - inspectorWidth; }
    void applySceneSettings(groovemorph::core::VariationSettings settings);
    juce::uint32 lockNoticeUntil = 0;
    void previewCandidate(const groovemorph::core::VariationSettings& settings);
    void refreshSceneControls();
    void updateTransportButtons();
    void confirmReset();
    void editOperation(std::size_t index, int field); // 0 enable, 1 depth, 2 slice
    void editName();
    void editMidiMap();
    void finishPreview(bool apply);
    void addSlider(groovemorph::ui::MenuSlider&, const juce::String& parameter, const juce::String& name);
    void addCombo(groovemorph::ui::MenuComboBox&, const juce::String& parameter,
                  const juce::StringArray& choices, const juce::String& name);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMorphAudioProcessorEditor)
};
