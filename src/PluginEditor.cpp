#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/control/VariationBlend.h"
#include "ui/theme/Theme.h"

#include <algorithm>

namespace theme = groovemorph::ui::theme;

namespace
{
juce::String describeTiming(const GrooveMorphAudioProcessor& processor)
{
    using groovemorph::core::TimingSource;
    namespace Missing = groovemorph::core::MissingField;
    if (!processor.isPrepared()) return "Timing: audio not running";
    if (!processor.hasValidTiming()) return "Timing: unavailable; check host/manual settings";
    juce::String text = "Timing: ";
    switch (processor.getTimingSource())
    {
        case TimingSource::Host: text << "host"; break;
        case TimingSource::Mixed: text << "host + manual"; break;
        case TimingSource::Manual: text << "manual"; break;
        case TimingSource::FreeRun: text << "free-run"; break;
    }
    juce::StringArray missing;
    const auto fields = processor.getMissingTimingFields();
    if ((fields & Missing::tempo) != 0) missing.add("tempo");
    if ((fields & Missing::meter) != 0) missing.add("meter");
    if ((fields & Missing::playState) != 0) missing.add("play state");
    if ((fields & Missing::position) != 0) missing.add("position");
    if ((fields & Missing::ppq) != 0) missing.add("PPQ (quantized requests held)");
    if ((fields & Missing::barAnchor) != 0) missing.add("bar anchor (beat/bar requests held)");
    if (!missing.isEmpty()) text << " | host missing " << missing.joinIntoString(", ");
    return text;
}

juce::String sceneLetter(std::size_t index)
{
    return juce::String::charToString(static_cast<juce::juce_wchar>('A' + index));
}

// Text of the F1 popover.
const char* const helpText =
    "Keyboard shortcuts\n"
    "  Space          Play / stop (internal transport)\n"
    "  1 - 4          Switch to scene A - D\n"
    "  T              Trigger the queued scene change\n"
    "  Esc            Cancel the queued scene change (closes the Timing panel first)\n"
    "  Ctrl/Cmd+Z     Undo the last scene edit or pending Surprise\n"
    "  Ctrl/Cmd+Shift+Z or Ctrl+Y   Redo\n"
    "  F1             This help\n\n"
    "Tips\n"
    "  Hover any control: the line at the bottom explains it.\n"
    "  Right-click a knob or slider for exact values, reset and MIDI learn.\n"
    "  Double-click a slider to return it to its default.\n"
    "  Timing... under the scenes holds tempo, meter, timing method and phrase length.\n"
    "  Settings (gear) has Demo loop, Reset to defaults, UI scale and the first-run guide.";

class HelpContent final : public juce::Component
{
public:
    HelpContent()
    {
        label.setText(helpText, juce::dontSendNotification);
        label.setFont(theme::font(theme::fontBody));
        label.setColour(juce::Label::textColourId, theme::text);
        label.setJustificationType(juce::Justification::topLeft);
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
        setSize(480, 320);
    }
    void resized() override { label.setBounds(getLocalBounds().reduced(theme::space3)); }

private:
    juce::Label label;
};
} // namespace

GrooveMorphAudioProcessorEditor::GrooveMorphAudioProcessorEditor(GrooveMorphAudioProcessor& processor)
    : AudioProcessorEditor(&processor), audioProcessor(processor),
      header(processor), band(processor), strip(processor), rack(processor), footer(processor), timingPanel(processor)
{
    setLookAndFeel(&lookAndFeel);
    setWantsKeyboardFocus(true);

    // ---- header ----
    header.playButton.onClick = [this] { audioProcessor.startInternalTransport(); updateTransportButtons(); };
    header.stopButton.onClick = [this] { audioProcessor.stopInternalTransport(); updateTransportButtons(); };
    header.easyButton.onClick = [this] { applyEasyMode(true); };
    header.settingsButton.onClick = [this] { showSettingsMenu(); };
    header.visualButton.onClick = [this] {
        if (header.visualButton.getToggleState())
        {
            if (visualWindow == nullptr)
                visualWindow = std::make_unique<groovemorph::ui::visual::VisualWindow>(audioProcessor, [this] {
                    header.visualButton.setToggleState(false, juce::dontSendNotification);
                });
            visualWindow->open();
        }
        else if (visualWindow != nullptr)
        {
            visualWindow->close();
        }
    };
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "bypass", header.bypassButton));
    header.presetBar.onChanged = [this] { refreshSceneControls(); };

    // ---- performance band ----
    for (std::size_t i = 0; i < band.pads.size(); ++i)
        band.pads[i].onClick = [this, i] {
            audioProcessor.cancelSurprise(); // a pending Surprise belongs to the scene that was sounding
            audioProcessor.requestVariation(i);
        };
    band.triggerButton.onClick = [this] { audioProcessor.triggerManualSwitch(); };
    band.cancelButton.onClick = [this] { audioProcessor.cancelQueuedSwitch(); };
    band.surpriseButton.onClick = [this] { doSurprise(); };
    band.undoButton.onClick = [this] { doUndo(); };
    band.redoButton.onClick = [this] { doRedo(); };

    // ---- timing strip and panel ----
    strip.timingButton.onClick = [this] { showTimingPanel(!timingPanel.isVisible()); };
    timingPanel.onClose = [this] { showTimingPanel(false); };

    // ---- effects rack ----
    rack.editVariation.onChange = [this]
    {
        if (refreshing) return;
        // Pending preview edits must be explicitly applied or reverted before changing target.
        if (audioProcessor.isAuditioning())
        {
            rack.editVariation.setSelectedId(static_cast<int>(editingIndex) + 1, juce::dontSendNotification);
            return;
        }
        editingIndex = static_cast<std::size_t>(juce::jmax(0, rack.editVariation.getSelectedId() - 1));
        refreshSceneControls();
    };
    rack.copyActiveFxButton.onClick = [this]
    {
        if (audioProcessor.copyVariationSettings(audioProcessor.getActiveVariation(), editingIndex))
            refreshSceneControls();
    };
    rack.variationName.onReturnKey = [this] { editName(); };
    rack.variationName.onFocusLost = [this] { editName(); };
    rack.previewButton.onClick = [this]
    {
        editName();
        editedVariation = audioProcessor.getSceneBank().variation(editingIndex);
        audioProcessor.beginAudition(editingIndex);
        comparingStored = false;
        timerCallback();
    };
    rack.compareButton.onClick = [this]
    {
        comparingStored = !comparingStored;
        audioProcessor.setAuditionComparingStored(comparingStored);
        timerCallback();
    };
    rack.applyButton.onClick = [this] { finishPreview(true); };
    rack.revertButton.onClick = [this] { finishPreview(false); };
    for (auto& card : rack.cards)
        card->onEdit = [this](std::size_t index, int field) { editOperation(index, field); };

    // ---- footer: MIDI note of the edited scene ----
    footer.midiNoteSlider.onDragStart = [this] { draggingMidi = true; };
    footer.midiNoteSlider.onDragEnd = [this] { draggingMidi = false; editMidiMap(); };
    // Keyboard and wheel steps pass through other scenes' notes; wait for a pause so only the note the user
    // stops on can raise the swap question. Drags and typed values still apply immediately.
    footer.midiNoteSlider.onValueChange = [this] { if (!draggingMidi && !refreshing) midiEditDue = juce::Time::getMillisecondCounter() + 600; };
    footer.returnOnNoteOff.onClick = [this] { editMidiMap(); };

    // ---- the inspector is the central workspace ----
    groovemorph::ui::SceneEditAccess access;
    access.editingIndex = [this] { return editingIndex; };
    access.settings = [this] { return editedVariation.settings; };
    access.setSettings = [this](const groovemorph::core::VariationSettings& settings) { applySceneSettings(settings); };
    access.storedSettings = [this] { return audioProcessor.getSceneBank().variation(editingIndex).settings; };
    access.previewCandidate = [this](const groovemorph::core::VariationSettings& settings) { previewCandidate(settings); };
    access.previewing = [this] { return audioProcessor.isAuditioning(); };
    inspector = std::make_unique<groovemorph::ui::InspectorPanel>(audioProcessor, std::move(access));

    // Children in visual order, so Tab follows what the eye sees.
    addAndMakeVisible(header);
    addAndMakeVisible(band);
    addAndMakeVisible(strip);
    addAndMakeVisible(rack);
    addAndMakeVisible(*inspector);
    addAndMakeVisible(footer);
    addChildComponent(timingPanel);

    easyView = std::make_unique<groovemorph::ui::EasyView>(audioProcessor);
    easyView->onOpenFullView = [this] { applyEasyMode(false); };
    addChildComponent(*easyView);
    refreshSceneControls();
    applyUiScale();
    setResizable(true, true);
    setResizeLimits(theme::minWidth, theme::minHeight, theme::maxWidth, theme::maxHeight);
    groovemorph::ui::finishControlMenus(*this, audioProcessor);
    setSize(theme::defaultWidth, theme::defaultHeight);
    applyEasyMode(audioProcessor.isEasyMode());
    startTimerHz(10);
    fastTimer.tick = [this] {
        if (easyShown) return;
        strip.tick();
        band.tickPads();
    };
    fastTimer.startTimerHz(30);
}

GrooveMorphAudioProcessorEditor::~GrooveMorphAudioProcessorEditor()
{
    fastTimer.stopTimer();
    stopTimer();
    audioProcessor.cancelMidiLearn(); // a closed editor must not leave MIDI learn armed
    visualWindow.reset(); // closes the Visual window and deactivates tracing
    // A hidden preview must never remain active after the editor closes.
    if (audioProcessor.isAuditioning()) audioProcessor.revertAudition();
    setLookAndFeel(nullptr); // before the look-and-feel member and the children are destroyed
}

void GrooveMorphAudioProcessorEditor::applyUiScale()
{
    // Small displays (e.g. 1024x768): shrink the whole editor to fit instead of cropping it. The user's UI scale
    // (settings menu, saved in the plugin state) multiplies that.
    float base = 1.0f;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        const float fit = juce::jmin(1.0f, static_cast<float>(area.getWidth() - 40) / static_cast<float>(theme::defaultWidth),
                                     static_cast<float>(area.getHeight() - 100) / static_cast<float>(theme::defaultHeight));
        if (fit < 1.0f && fit > 0.0f)
            base = juce::jmax(0.5f, fit);
    }
    setScaleFactor(base * static_cast<float>(audioProcessor.getUiScalePercent()) / 100.0f);
}

void GrooveMorphAudioProcessorEditor::refreshSceneControls()
{
    const juce::ScopedValueSetter<bool> guard(refreshing, true);
    const auto bank = audioProcessor.getSceneBank();
    if (!audioProcessor.isAuditioning()) editedVariation = bank.variation(editingIndex);
    rack.variationName.setText(juce::String(editedVariation.name), false);
    for (std::size_t i = 0; i < rack.cards.size(); ++i)
    {
        const auto& settings = editedVariation.settings.operations[i];
        rack.cards[i]->setOperation(settings.enabled, settings.depth, settings.sliceBeats);
    }
    footer.midiNoteSlider.setValue(bank.midiNoteMap().notes[editingIndex], juce::dontSendNotification);
    footer.returnOnNoteOff.setToggleState(bank.midiNoteMap().noteOffPolicy ==
        groovemorph::core::scene::NoteOffPolicy::ReturnToPrevious, juce::dontSendNotification);
    timerCallback();
    if (inspector != nullptr) inspector->refresh();
}

void GrooveMorphAudioProcessorEditor::applySceneSettings(groovemorph::core::VariationSettings settings)
{
    if (refreshing) return;
    // C03: locked fields are kept at their stored values for manual edits too. The lock
    // configuration being edited is kept; the locks in effect are those of the stored scene.
    const auto stored = audioProcessor.getSceneBank().variation(editingIndex).settings;
    if (groovemorph::core::control::enforcePermissions(stored, settings, true, audioProcessor.getGridContext().phraseTicks()) > 0)
        lockNoticeUntil = juce::Time::getMillisecondCounter() + 3000;
    editedVariation.settings = settings;
    if (audioProcessor.isAuditioning()) audioProcessor.setAuditionEdit(editedVariation);
    else audioProcessor.setVariationSettings(editingIndex, settings);
    refreshSceneControls();
}

void GrooveMorphAudioProcessorEditor::previewCandidate(const groovemorph::core::VariationSettings& candidateSettings)
{
    // Generated or recalled candidates still honour the locks that are in force now.
    auto settings = candidateSettings;
    const auto stored = audioProcessor.getSceneBank().variation(editingIndex).settings;
    if (groovemorph::core::control::enforcePermissions(stored, settings, true, audioProcessor.getGridContext().phraseTicks()) > 0)
        lockNoticeUntil = juce::Time::getMillisecondCounter() + 3000;
    if (!audioProcessor.isAuditioning())
    {
        editName();
        audioProcessor.beginAudition(editingIndex);
        editedVariation = audioProcessor.getSceneBank().variation(editingIndex);
        comparingStored = false;
    }
    editedVariation.settings = settings;
    audioProcessor.setAuditionEdit(editedVariation);
    refreshSceneControls();
}

void GrooveMorphAudioProcessorEditor::editOperation(std::size_t index, int field)
{
    if (refreshing) return;
    // Write back only the control that changed, so enabling an effect never rounds the depth/slice of
    // a generated or restored value to the slider step.
    const auto& card = *rack.cards[index];
    auto settings = editedVariation.settings;
    auto& op = settings.operations[index];
    if (field == 0) op.enabled = card.enabled.getToggleState();
    else if (field == 1) op.depth = static_cast<float>(card.depth.getValue());
    else op.sliceBeats = static_cast<float>(card.slice.getValue());
    applySceneSettings(settings);
}

void GrooveMorphAudioProcessorEditor::editName()
{
    if (refreshing) return;
    editedVariation.name = groovemorph::core::scene::sanitizeName(rack.variationName.getText().toStdString());
    if (audioProcessor.isAuditioning()) audioProcessor.setAuditionEdit(editedVariation);
    else audioProcessor.setVariationName(editingIndex, editedVariation.name);
    rack.variationName.setText(juce::String(editedVariation.name), false);
}

void GrooveMorphAudioProcessorEditor::editMidiMap()
{
    if (refreshing || audioProcessor.isAuditioning() || midiSwapDialogOpen) return;
    namespace scene = groovemorph::core::scene;
    auto map = audioProcessor.getSceneBank().midiNoteMap();
    map.noteOffPolicy = footer.returnOnNoteOff.getToggleState()
        ? scene::NoteOffPolicy::ReturnToPrevious
        : scene::NoteOffPolicy::Ignore;
    const int requested = static_cast<int>(footer.midiNoteSlider.getValue());
    const auto plan = map.planAssign(editingIndex, requested);
    if (plan.kind != scene::NoteAssignPlan::Kind::Conflict)
    {
        (void) map.assign(editingIndex, requested, scene::ConflictChoice::Swap); // Free: just sets the note
        audioProcessor.setMidiNoteMap(map);
        return;
    }

    // A04: another scene already owns the note. Never leave a silent duplicate: ask, and swap on request.
    audioProcessor.setMidiNoteMap(map); // the note-off policy part of this edit still applies
    midiSwapDialogOpen = true;
    const auto bank = audioProcessor.getSceneBank();
    auto sceneName = [&bank](std::size_t i) {
        return juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)) + " (" + juce::String(bank.variation(i).name) + ")";
    };
    juce::String text;
    text << "Note " << requested << " already triggers scene " << sceneName(plan.owner) << ".\n\n"
         << "Swap: scene " << sceneName(editingIndex) << " takes note " << requested << " and scene "
         << sceneName(plan.owner) << " gets note " << plan.ownerNewNote << ".\n"
         << "Cancel: nothing changes. A swap is one step in Undo.";
    const auto index = editingIndex;
    juce::Component::SafePointer<GrooveMorphAudioProcessorEditor> safe(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions::makeOptionsOkCancel(
            juce::MessageBoxIconType::QuestionIcon, "Note " + juce::String(requested) + " is already used", text,
            "Swap", "Cancel", this),
        [safe, index, requested](int result) {
            if (safe == nullptr)
                return;
            safe->midiSwapDialogOpen = false;
            if (result == 1)
            {
                auto current = safe->audioProcessor.getSceneBank().midiNoteMap();
                if (current.assign(index, requested, groovemorph::core::scene::ConflictChoice::Swap))
                    safe->audioProcessor.setMidiNoteMap(current);
            }
            safe->refreshSceneControls(); // Cancel: the slider returns to the stored note
        });
}

void GrooveMorphAudioProcessorEditor::finishPreview(bool apply)
{
    if (apply) { editName(); audioProcessor.commitAudition(); }
    else audioProcessor.revertAudition();
    comparingStored = false;
    refreshSceneControls();
}

void GrooveMorphAudioProcessorEditor::setNotice(const juce::String& text)
{
    noticeText = text;
    noticeUntil = juce::Time::getMillisecondCounter() + 6000;
    timerCallback();
}

void GrooveMorphAudioProcessorEditor::doSurprise()
{
    // Same calls and messages as the Easy view's Surprise me.
    using Status = GrooveMorphAudioProcessor::SurpriseStatus;
    if (audioProcessor.isSurprisePending())
        audioProcessor.cancelSurprise(); // pressing again asks for the next candidate
    const auto scene = sceneLetter(audioProcessor.getActiveVariation());
    switch (audioProcessor.requestSurprise())
    {
        case Status::Armed:
            setNotice("Surprise: playing a new variation of scene " + scene + ", kept at the next phrase (Undo takes it back)");
            break;
        case Status::NothingToChange:
            setNotice("Nothing to change: scene " + scene + " is locked or already as busy as it gets");
            break;
        case Status::Busy:
            setNotice("A preview is open. Apply or revert it first, then try Surprise again");
            break;
    }
}

void GrooveMorphAudioProcessorEditor::doUndo()
{
    if (audioProcessor.isSurprisePending())
    {
        audioProcessor.cancelSurprise();
        setNotice("Surprise cancelled, the scene is as it was");
    }
    else if (!audioProcessor.isAuditioning())
    {
        if (audioProcessor.undoSceneEdit())
            setNotice("Undid the last scene edit");
    }
    refreshSceneControls();
}

void GrooveMorphAudioProcessorEditor::doRedo()
{
    if (!audioProcessor.isAuditioning())
    {
        audioProcessor.redoSceneEdit();
        setNotice("Redid the scene edit");
    }
    refreshSceneControls();
}

void GrooveMorphAudioProcessorEditor::updateHint()
{
    juce::String hint;
    if (auto* under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse())
    {
        if (isParentOf(under))
        {
            for (auto* c = under; c != nullptr && c != this; c = c->getParentComponent())
            {
                if (auto* client = dynamic_cast<juce::TooltipClient*>(c))
                {
                    hint = client->getTooltip();
                    if (hint.isNotEmpty())
                        break;
                }
            }
        }
    }
    footer.setHint(hint);
}

void GrooveMorphAudioProcessorEditor::timerCallback()
{
    if (midiEditDue != 0 && static_cast<juce::int32>(juce::Time::getMillisecondCounter() - midiEditDue) >= 0)
    {
        midiEditDue = 0;
        editMidiMap();
    }
    updateTransportButtons();
    header.presetBar.refresh();
    const auto timingText = describeTiming(audioProcessor);
    strip.timingLabel.setText(timingText, juce::dontSendNotification);
    strip.timingLabel.setTooltip(timingText);
    const auto bank = audioProcessor.getSceneBank();
    const auto active = audioProcessor.getActiveVariation();
    const auto queued = audioProcessor.getQueuedVariation();
    for (std::size_t i = 0; i < band.pads.size(); ++i)
    {
        groovemorph::ui::shell::ScenePad::State state;
        state.name = juce::String(bank.variation(i).name);
        state.sounding = i == active;
        state.queued = static_cast<int>(i) == queued;
        state.editing = i == editingIndex;
        band.pads[i].setState(state);
    }
    juce::String status = audioProcessor.isTransportPlaying() ? "Playing" : "Stopped";
    status << " | editing " << sceneLetter(editingIndex)
           << (editingIndex == active ? " (sounding)" : " (not sounding)") << " | bar " << juce::String(audioProcessor.getBarInPhrase() + 1.0, 2)
           << " | beat " << juce::String(audioProcessor.getBeatInBar() + 1.0, 2);
    if (queued >= 0)
    {
        const auto samples = audioProcessor.getSamplesUntilSwitch();
        status << " | queued " << sceneLetter(static_cast<std::size_t>(queued));
        if (samples >= 0) status << " in " << juce::String(samples) << " samples";
        else status << " (waiting for trigger/transport)";
    }
    const bool preview = audioProcessor.isAuditioning();
    const bool surprise = preview && audioProcessor.isSurprisePending();
    // Host state restore can replace scene data while the editor remains open.
    if (!preview && !rack.anyCardDragging() && !rack.variationName.hasKeyboardFocus(true)
        && !(editedVariation == bank.variation(editingIndex)))
        refreshSceneControls();
    if (!preview && !draggingMidi && !footer.midiNoteSlider.hasKeyboardFocus(true))
    {
        footer.midiNoteSlider.setValue(bank.midiNoteMap().notes[editingIndex], juce::dontSendNotification);
        footer.returnOnNoteOff.setToggleState(bank.midiNoteMap().noteOffPolicy ==
            groovemorph::core::scene::NoteOffPolicy::ReturnToPrevious, juce::dontSendNotification);
    }
    if (audioProcessor.consumeMidiLinkNotice() == GrooveMorphAudioProcessor::midiLinkNoticeTableFull)
    {
        midiNoticeUntil = juce::Time::getMillisecondCounter() + 8000;
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "MIDI link table full",
            "GrooveMorph can hold 64 MIDI links and all 64 are in use, so this control was not linked. "
            "Right-click a linked control and choose Unlink MIDI to free a slot, then try again.",
            "OK", this);
    }
    const auto now = juce::Time::getMillisecondCounter();
    if (now < midiNoticeUntil) status << " | MIDI link table full (64): not linked";
    if (now < lockNoticeUntil) status << " | locked field(s) kept (Generator tab > Locks)";
    if (now < noticeUntil) status << " | " << noticeText;
    if (preview && !surprise)
        status << (comparingStored ? " | Preview: stored" : " | Preview: edited")
               << (audioProcessor.isAuditionDirty() ? " (unapplied)" : "");
    const auto recovery = audioProcessor.getLastSceneLoadReport();
    if (recovery.status != groovemorph::core::scene::LoadStatus::Ok)
        status << " | Scene state recovered (see tooltip)";
    strip.statusLabel.setText(status, juce::dontSendNotification);
    juce::String details = status;
    for (const auto& message : recovery.messages) details << "\n" << juce::String(message);
    strip.statusLabel.setTooltip(details);

    // A pending Surprise is managed with Undo (cancel) or kept automatically at the next phrase; the preview
    // buttons and the effect cards stay locked meanwhile so they cannot overwrite the candidate.
    rack.compareButton.setButtonText(comparingStored ? "Hear edited" : "Hear stored");
    rack.previewButton.setEnabled(!preview);
    rack.compareButton.setEnabled(preview && !surprise);
    rack.applyButton.setEnabled(preview && !surprise);
    rack.revertButton.setEnabled(preview && !surprise);
    rack.editVariation.setEnabled(!preview);
    rack.copyActiveFxButton.setEnabled(!preview && active != editingIndex);
    for (auto& card : rack.cards)
        card->setEnabled(!surprise);
    rack.variationName.setEnabled(!surprise);
    band.undoButton.setEnabled(surprise || (!preview && audioProcessor.canUndoSceneEdit()));
    band.redoButton.setEnabled(!preview && audioProcessor.canRedoSceneEdit());
    footer.midiNoteSlider.setEnabled(!preview && !midiSwapDialogOpen);
    footer.returnOnNoteOff.setEnabled(!preview && !midiSwapDialogOpen);
    band.cancelButton.setEnabled(queued >= 0);
    // The scheduler captures the request policy; changing the selector does not
    // rewrite it. Keep manual triggering available for any armed request.
    band.triggerButton.setEnabled(queued >= 0);
    if (const auto duplicateNote = bank.midiNoteMap().firstConflictingNote())
    {
        // Keep existing project maps intact, but make first-match priority visible.
        juce::String text("MIDI conflict: note ");
        text << *duplicateNote << " maps to multiple variations";
        footer.midiLabel.setText(text, juce::dontSendNotification);
        juce::String tooltip("Note ");
        tooltip << *duplicateNote
                << " is assigned to multiple variations. The first mapped variation wins. "
                   "Set the note of one of them again (you will be offered a swap) to restore independent triggering. "
                   "Received note events: "
                << juce::String(audioProcessor.getMidiNoteEventCount());
        footer.midiLabel.setTooltip(tooltip);
    }
    else
    {
        footer.midiLabel.setText(audioProcessor.getMidiNoteEventCount() == 0
            ? "MIDI: no notes received; route host MIDI to GrooveMorph"
            : "MIDI: " + juce::String(audioProcessor.getMidiNoteEventCount()) + " note events received",
            juce::dontSendNotification);
        footer.midiLabel.setTooltip("Incoming MIDI note events from the host");
    }
    if (inspector != nullptr && !refreshing) inspector->refresh();
    if (easyShown && easyView != nullptr) easyView->refresh();
    if (!easyShown) updateHint();
}

void GrooveMorphAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(theme::bg);
}

void GrooveMorphAudioProcessorEditor::resized()
{
    // Proportional layout from the current bounds: fixed-height bands, the workspace takes the rest.
    auto area = getLocalBounds().reduced(theme::space2);
    const int gap = theme::space2;
    header.setBounds(area.removeFromTop(groovemorph::ui::shell::HeaderBar::height));
    area.removeFromTop(gap);
    band.setBounds(area.removeFromTop(groovemorph::ui::shell::PerformanceBand::height));
    area.removeFromTop(gap);
    strip.setBounds(area.removeFromTop(groovemorph::ui::shell::TimingStrip::height));
    area.removeFromTop(gap);
    rack.setBounds(area.removeFromTop(groovemorph::ui::shell::EffectsRack::height));
    area.removeFromTop(gap);
    footer.setBounds(area.removeFromBottom(groovemorph::ui::shell::FooterBar::height));
    area.removeFromBottom(gap);
    if (inspector != nullptr) inspector->setBounds(area);
    if (easyView != nullptr) easyView->setBounds(getLocalBounds());
    layoutTimingPanel();
}

void GrooveMorphAudioProcessorEditor::layoutTimingPanel()
{
    const int width = std::min(groovemorph::ui::shell::TimingPanel::preferredWidth, getWidth() - 2 * theme::space2);
    const int panelHeight = groovemorph::ui::shell::TimingPanel::preferredHeight;
    const int x = std::max(theme::space2, strip.getRight() - width);
    timingPanel.setBounds(x, strip.getBottom() + 4, width, panelHeight);
}

void GrooveMorphAudioProcessorEditor::showTimingPanel(bool show)
{
    if (show)
    {
        layoutTimingPanel();
        timingPanel.setVisible(true);
        timingPanel.toFront(false);
        timingPanel.grabKeyboardFocus();
    }
    else
    {
        const bool hadFocus = timingPanel.hasKeyboardFocus(true);
        timingPanel.setVisible(false);
        if (hadFocus)
            grabKeyboardFocus();
    }
}

void GrooveMorphAudioProcessorEditor::mouseDown(const juce::MouseEvent&)
{
    grabKeyboardFocus(); // shortcuts work after clicking empty space
    if (timingPanel.isVisible())
        showTimingPanel(false);
}

void GrooveMorphAudioProcessorEditor::showSettingsMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Demo loop", true, audioProcessor.isDemoSourceEnabled());
    menu.addItem(2, "Reset to defaults...");
    menu.addSeparator();
    juce::PopupMenu scale;
    for (const int percent : {75, 100, 125, 150, 200})
        scale.addItem(1000 + percent, juce::String(percent) + " %", true, audioProcessor.getUiScalePercent() == percent);
    menu.addSubMenu("UI scale", scale);
    menu.addSeparator();
    menu.addItem(3, "Show guide");
    menu.addItem(4, "Help (F1)");
    juce::Component::SafePointer<GrooveMorphAudioProcessorEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&header.settingsButton).withMinimumWidth(200),
                       [safe](int result) {
                           if (safe == nullptr || result == 0)
                               return;
                           auto& self = *safe;
                           if (result == 1)
                               self.audioProcessor.setDemoSourceEnabled(!self.audioProcessor.isDemoSourceEnabled());
                           else if (result == 2)
                               self.confirmReset();
                           else if (result == 3)
                           {
                               self.applyEasyMode(true);
                               if (self.easyView != nullptr) self.easyView->showGuide();
                           }
                           else if (result == 4)
                               self.showHelp();
                           else if (result >= 1000)
                           {
                               self.audioProcessor.setUiScalePercent(result - 1000);
                               self.applyUiScale();
                           }
                       });
}

void GrooveMorphAudioProcessorEditor::showHelp()
{
    juce::CallOutBox::launchAsynchronously(std::make_unique<HelpContent>(),
                                           getLocalArea(&header, header.settingsButton.getBounds()), this);
}

void GrooveMorphAudioProcessorEditor::applyEasyMode(bool on)
{
    // N05: the Easy view is a child on top of everything; the editor's own controls are hidden (and restored
    // afterwards) rather than removed, so their callbacks, attachments and the inspector keep working.
    audioProcessor.setEasyMode(on);
    if (easyView == nullptr || on == easyShown)
        return;
    easyShown = on;
    if (on)
    {
        showTimingPanel(false);
        hiddenByEasy.clear();
        for (auto* child : getChildren())
            if (child != easyView.get() && child->isVisible())
            {
                hiddenByEasy.emplace_back(child);
                child->setVisible(false);
            }
        easyView->setBounds(getLocalBounds());
        easyView->setVisible(true);
        easyView->toFront(false);
        easyView->refresh();
    }
    else
    {
        easyView->setVisible(false);
        for (auto& child : hiddenByEasy)
            if (child != nullptr)
                child->setVisible(true);
        hiddenByEasy.clear();
        refreshSceneControls();
    }
    repaint();
}

void GrooveMorphAudioProcessorEditor::updateTransportButtons()
{
    const bool internal = audioProcessor.isInternalTransportActive();
    const bool playing = audioProcessor.isTransportPlaying();
    header.playButton.setToggleState(playing, juce::dontSendNotification);
    header.stopButton.setToggleState(!playing, juce::dontSendNotification);
    header.playButton.setEnabled(internal);
    header.stopButton.setEnabled(internal);
    if (!internal)
    {
        const juce::String why = "The host controls play and stop here; these buttons only show its transport state ("
                                 + juce::String(playing ? "playing" : "stopped") + "). Use the host's transport, or choose Timing method = Sample Clock without a host play state to use them.";
        header.playButton.setTooltip(why);
        header.stopButton.setTooltip(why);
    }
}

void GrooveMorphAudioProcessorEditor::confirmReset()
{
    juce::Component::SafePointer<GrooveMorphAudioProcessorEditor> safe(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions::makeOptionsOkCancel(
            juce::MessageBoxIconType::WarningIcon, "Reset everything?",
            "This puts every setting back to its factory default: all controls, the four scenes (effects, names, placement, MIDI notes), "
            "MIDI links, loaded loops, evolution and gestures. Your current scenes will be replaced.",
            "Reset", "Cancel", this),
        [safe](int result) {
            if (safe == nullptr || result != 1)
                return;
            safe->audioProcessor.resetToDefaults();
            safe->editingIndex = 0;
            safe->comparingStored = false;
            safe->rack.editVariation.setSelectedId(1, juce::dontSendNotification);
            safe->refreshSceneControls();
            safe->updateTransportButtons();
        });
}

bool GrooveMorphAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (key == juce::KeyPress::F1Key)
    {
        showHelp();
        return true;
    }
    // Undo / redo of scene edits (the same as the Undo and Redo buttons).
    if (mods.isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z'))
    {
        if (mods.isShiftDown()) doRedo();
        else doUndo();
        return true;
    }
    if (mods.isCtrlDown() && !mods.isShiftDown() && (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y'))
    {
        doRedo();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && timingPanel.isVisible())
    {
        showTimingPanel(false);
        return true;
    }
    if (key == juce::KeyPress::spaceKey && audioProcessor.isInternalTransportActive())
    {
        if (audioProcessor.isInternalPlaying())
            audioProcessor.stopInternalTransport();
        else
            audioProcessor.startInternalTransport();
        updateTransportButtons();
        return true;
    }
    // A07 / F04 section 3.1: 1-4 request scene A-D, T fires the queued switch (Manual mode), Esc cancels it.
    // A focused text field consumes its own characters before the editor sees the key, so typing is unaffected.
    if (!mods.isAnyModifierKeyDown())
    {
        const auto c = key.getTextCharacter();
        if (c >= '1' && c <= '4')
        {
            audioProcessor.cancelSurprise();
            audioProcessor.requestVariation(static_cast<std::size_t>(c - '1'));
            return true;
        }
        if (c == 't' || c == 'T')
        {
            audioProcessor.triggerManualSwitch();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            audioProcessor.cancelQueuedSwitch();
            return true;
        }
    }
    return false;
}

juce::AudioProcessorEditor* GrooveMorphAudioProcessor::createEditor()
{
    ensureServices(); // W3: the service timer starts lazily (KNOWN-ISSUES #4)
    return new GrooveMorphAudioProcessorEditor(*this);
}
