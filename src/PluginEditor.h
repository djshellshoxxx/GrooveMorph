#pragma once

#include <JuceHeader.h>
#include "core/scene/SceneBank.h"
#include "ui/InspectorPanel.h"
#include "ui/ControlMenu.h"
#include "ui/EasyView.h"
#include "ui/shell/EffectsRack.h"
#include "ui/shell/FooterBar.h"
#include "ui/shell/HeaderBar.h"
#include "ui/shell/PerformanceBand.h"
#include "ui/shell/TimingPanel.h"
#include "ui/shell/TimingStrip.h"
#include "ui/theme/GrooveLookAndFeel.h"
#include "ui/visual/VisualWindow.h"

#include <functional>
#include <memory>
#include <vector>

class GrooveMorphAudioProcessor;

// The editor composes the shell components (docs/specs/UI-REDESIGN.md): header, performance band, timing strip,
// effects rack, the inspector as the central workspace and the footer. It owns the scene-editing state
// (edited scene, preview/audition flow, MIDI note swap dialog) and the timers.
class GrooveMorphAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit GrooveMorphAudioProcessorEditor(GrooveMorphAudioProcessor& processor);
    ~GrooveMorphAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void showInspectorTab(int index) { if (inspector != nullptr) inspector->showTab(index); }

private:
    GrooveMorphAudioProcessor& audioProcessor;
    // Declared first so it is destroyed last; the destructor detaches it from the editor before any child dies.
    groovemorph::ui::theme::GrooveLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow{this, 700};
    groovemorph::ui::shell::HeaderBar header;
    groovemorph::ui::shell::PerformanceBand band;
    groovemorph::ui::shell::TimingStrip strip;
    groovemorph::ui::shell::EffectsRack rack;
    groovemorph::ui::shell::FooterBar footer;
    groovemorph::ui::shell::TimingPanel timingPanel;
    std::unique_ptr<groovemorph::ui::visual::VisualWindow> visualWindow;
    std::unique_ptr<groovemorph::ui::InspectorPanel> inspector;
    // N05 Easy mode: a full-size view above the editor's own controls (hidden while the full view is shown).
    std::unique_ptr<groovemorph::ui::EasyView> easyView;
    std::vector<juce::Component::SafePointer<juce::Component>> hiddenByEasy;
    bool easyShown = false;
    void applyEasyMode(bool on);

    // 30 Hz: dirty-rectangle repaints for the phrase cursor and the scene pads only.
    struct FastTimer final : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { if (tick) tick(); }
    };
    FastTimer fastTimer;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::size_t editingIndex = 0;
    groovemorph::core::scene::Variation editedVariation;
    bool refreshing = false, comparingStored = false, draggingMidi = false;
    bool midiSwapDialogOpen = false;     // A04: a note-swap confirmation is showing
    juce::uint32 midiNoticeUntil = 0;    // A04: "MIDI link table full" shown in the status line until then
    juce::uint32 midiEditDue = 0;        // A04: keyboard/wheel note steps are applied after a short pause (0 = none)
    juce::uint32 lockNoticeUntil = 0;
    juce::uint32 noticeUntil = 0;        // short message from Surprise / Undo / Redo shown in the status line
    juce::String noticeText;

    void timerCallback() override;
    void applySceneSettings(groovemorph::core::VariationSettings settings);
    void previewCandidate(const groovemorph::core::VariationSettings& settings);
    void refreshSceneControls();
    void updateTransportButtons();
    void confirmReset();
    void editOperation(std::size_t index, int field); // 0 enable, 1 depth, 2 slice
    void editName();
    void editMidiMap();
    void finishPreview(bool apply);
    void applyUiScale();
    void showSettingsMenu();
    void showHelp();
    void showTimingPanel(bool show);
    void layoutTimingPanel();
    void setNotice(const juce::String& text);
    void doSurprise();
    void doUndo();
    void doRedo();
    void updateHint();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveMorphAudioProcessorEditor)
};
