#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/control/VariationBlend.h"

#include <iterator>

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
void configureSlider(juce::Slider& slider, const juce::String& name)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 66, 24);
    slider.setName(name);
    slider.setTooltip(name);
}
}

void GrooveMorphAudioProcessorEditor::addSlider(groovemorph::ui::MenuSlider& slider,
    const juce::String& parameter, const juce::String& name)
{
    configureSlider(slider, name);
    slider.info() = {&audioProcessor, name, "p:" + parameter, {}};
    addAndMakeVisible(slider);
    sliderAttachments.push_back(std::make_unique<SliderAttachment>(audioProcessor.parameters, parameter, slider));
}

void GrooveMorphAudioProcessorEditor::addCombo(groovemorph::ui::MenuComboBox& combo,
    const juce::String& parameter, const juce::StringArray& choices, const juce::String& name)
{
    combo.addItemList(choices, 1);
    combo.setName(name);
    combo.setTooltip(name);
    combo.info() = {&audioProcessor, name, "p:" + parameter, {}};
    addAndMakeVisible(combo);
    comboAttachments.push_back(std::make_unique<ComboAttachment>(audioProcessor.parameters, parameter, combo));
}

GrooveMorphAudioProcessorEditor::GrooveMorphAudioProcessorEditor(GrooveMorphAudioProcessor& processor)
    : AudioProcessorEditor(&processor), audioProcessor(processor)
{
    titleLabel.setText("GrooveMorph", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    for (auto* label : {&titleLabel, &timingLabel, &statusLabel, &midiLabel})
        addAndMakeVisible(*label);
    addSlider(intensitySlider, "masterIntensity", "Intensity (0 = dry)");
    addSlider(dryWetSlider, "dryWet", "Dry / wet");
    addSlider(tempoSlider, "manualTempo", "Manual tempo (BPM)");
    addSlider(meterSlider, "manualMeterNumerator", "Manual beats per bar");
    addSlider(phraseSlider, "phraseBars", "Phrase length (bars)");
    intensitySlider.textFromValueFunction = [](double value) { return juce::String(value, 2); };
    dryWetSlider.textFromValueFunction = [](double value) { return juce::String(value, 2); };
    intensitySlider.setNumDecimalPlacesToDisplay(2);
    dryWetSlider.setNumDecimalPlacesToDisplay(2);
    tempoSlider.setNumDecimalPlacesToDisplay(2);
    meterSlider.setNumDecimalPlacesToDisplay(0);
    phraseSlider.setNumDecimalPlacesToDisplay(0);
    addCombo(timingMode, "timingMode", {"Automatic", "Host PPQ", "Sample Clock"}, "Timing method");
    timingMode.setTooltip("Automatic prefers host PPQ and falls back to samples. Host PPQ holds scheduled switches when PPQ is missing. Sample Clock free-runs without a host; a stopped host stays stopped.");
    addCombo(transitionMode, "transitionMode", {"Immediate", "Next Beat", "Next Bar", "Next Phrase", "Manual"}, "Transition");
    addCombo(denominator, "manualMeterDenominator", {"1", "2", "4", "8", "16"}, "Manual beat unit");
    intensitySlider.setTooltip("How strongly the rhythm effects are applied: 0 = none (you hear the unprocessed sound), 1 = full. Same control as the Intensity macro in the Perform tab.");
    dryWetSlider.setTooltip("Mix between the unprocessed sound (0) and the processed sound (1). Intensity and Dry/wet multiply, so either one at 0 gives the dry sound.");
    tempoSlider.setTooltip("Tempo in beats per minute (20-300), used when the host does not provide one and in Standalone. Changing it keeps the current position (no jump). Imported loops follow it: they play at original speed when it equals the loop's own tempo.");
    meterSlider.setTooltip("Beats per bar (1-16) used when the host does not provide a time signature. 4 means 4/4 or 4/8, 3 means 3/4 and so on. The beat unit box sets the note value of one beat.");
    denominator.setTooltip("Note value of one beat for the manual time signature: 4 = quarter note, 8 = eighth note, and so on. Used only when the host provides no time signature.");
    phraseSlider.setTooltip("Length of one phrase in bars (1-16). Scene changes can wait for a phrase boundary; placement windows, the phrase rings and morph/return timing follow this length.");
    transitionMode.setTooltip("When a scene change takes effect: Immediate, on the Next Beat, Next Bar, Next Phrase, or Manual (waits until you press Trigger queued).");
    bypassButton.setTooltip("Bypass: passes the sound through unchanged (a short fade avoids clicks). Host-automatable.");
    addAndMakeVisible(bypassButton);
    demoButton.onClick = [this] { audioProcessor.setDemoSourceEnabled(demoButton.getToggleState()); };
    demoButton.setTooltip("Switches the input source between live input and the built-in drum loop (kick, snare, hat) so the effects can be heard without external audio. Captured/imported loops are in the Input tab. Not saved with the project.");
    addAndMakeVisible(demoButton);
    playButton.setTooltip("Starts the internal transport from the beginning (Standalone, or Sample Clock timing without a host play state). Shortcut: Space.");
    stopButton.setTooltip("Stops the internal transport and returns to the beginning. Effects, the demo loop and loaded loops stop with it. Shortcut: Space.");
    resetButton.setTooltip("Resets everything to the factory defaults: all settings, the four scenes (effects, names, placement, MIDI notes), MIDI links, loaded loops, evolution and gestures. Asks first.");
    playButton.setClickingTogglesState(false);
    stopButton.setClickingTogglesState(false);
    playButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2e8b57));
    stopButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff9a5a2a));
    playButton.onClick = [this] { audioProcessor.startInternalTransport(); updateTransportButtons(); };
    stopButton.onClick = [this] { audioProcessor.stopInternalTransport(); updateTransportButtons(); };
    resetButton.onClick = [this] { confirmReset(); };
    for (auto* b : {&playButton, &stopButton, &resetButton})
        addAndMakeVisible(*b);
    phraseStripTip.setTooltip("The phrase: one box per bar. The cyan line is the current position inside the phrase.");
    addAndMakeVisible(phraseStripTip);
    visualButton.setTooltip("Opens or closes the separate Visual window: a live picture of what the plugin is doing to the sound (which past audio you are hearing, what is planned next, gate, scenes) with the numbers behind it. Tracing only runs while the window is open.");
    visualButton.onClick = [this] {
        if (visualButton.getToggleState())
        {
            if (visualWindow == nullptr)
                visualWindow = std::make_unique<groovemorph::ui::visual::VisualWindow>(audioProcessor, [this] {
                    visualButton.setToggleState(false, juce::dontSendNotification);
                });
            visualWindow->open();
        }
        else if (visualWindow != nullptr)
        {
            visualWindow->close();
        }
    };
    addAndMakeVisible(visualButton);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "bypass", bypassButton);
    bypassButton.info() = {&audioProcessor, "Bypass", "p:bypass", {}};
    tempoSlider.info().unit = "BPM";

    for (std::size_t i = 0; i < variationButtons.size(); ++i)
    {
        auto& button = variationButtons[i];
        button.onClick = [this, i] { audioProcessor.requestVariation(i); };
        button.setTooltip("Switch to this scene (A-D). It takes effect according to the Transition setting: now, next beat, bar, phrase, or when you press Trigger queued. Shows which scene is sounding, queued and being edited.");
        addAndMakeVisible(button);
    }
    triggerButton.setTooltip("Fires the queued scene change now. It only affects changes that were queued while Transition was set to Manual.");
    cancelButton.setTooltip("Cancels the queued scene change so the current scene keeps sounding.");
    triggerButton.onClick = [this] { audioProcessor.triggerManualSwitch(); };
    cancelButton.onClick = [this] { audioProcessor.cancelQueuedSwitch(); };
    editVariation.addItemList({"Edit A", "Edit B", "Edit C", "Edit D"}, 1);
    editVariation.setSelectedId(1, juce::dontSendNotification);
    editVariation.setName("Variation to edit");
    editVariation.setTooltip("Which scene (A-D) the controls below edit. It does not change what is playing. Locked while a preview is running: apply or revert the preview first.");
    editVariation.onChange = [this]
    {
        if (refreshing) return;
        // Pending preview edits must be explicitly applied or reverted before changing target.
        if (audioProcessor.isAuditioning())
        {
            editVariation.setSelectedId(static_cast<int>(editingIndex) + 1, juce::dontSendNotification);
            return;
        }
        editingIndex = static_cast<std::size_t>(juce::jmax(0, editVariation.getSelectedId() - 1));
        refreshSceneControls();
    };
    addAndMakeVisible(editVariation);
    variationName.setTooltip("Name of the scene being edited (shown on its button).");
    copyActiveFxButton.setTooltip(
        "Copy effect settings from the currently playing variation into the variation "
        "selected for editing. Keeps the target's name/MIDI note; one Undo restores it.");
    copyActiveFxButton.onClick = [this]
    {
        if (audioProcessor.copyVariationSettings(audioProcessor.getActiveVariation(), editingIndex))
            refreshSceneControls();
    };
    addAndMakeVisible(copyActiveFxButton);
    variationName.setName("Variation name");
    variationName.setInputRestrictions(static_cast<int>(groovemorph::core::scene::maxVariationNameLength));
    variationName.onReturnKey = [this] { editName(); };
    variationName.onFocusLost = [this] { editName(); };
    addAndMakeVisible(variationName);
    const juce::StringArray names{"Repeat / stutter", "Reverse slice", "Gate", "Half-time", "Tape stop", "Filter sweep", "Bit crush"};
    for (std::size_t i = 0; i < operations.size(); ++i)
    {
        auto& row = operations[i];
        row.enabled.setButtonText(names[static_cast<int>(i)]);
        configureSlider(row.depth, names[static_cast<int>(i)] + " depth");
        configureSlider(row.slice, names[static_cast<int>(i)] + " slice (meter beats)");
        row.enabled.info() = {&audioProcessor, names[static_cast<int>(i)] + " on/off (sounding scene)", "s:" + juce::String(static_cast<int>(i)) + ":enabled", {}};
        row.depth.info() = {&audioProcessor, names[static_cast<int>(i)] + " depth (sounding scene)", "s:" + juce::String(static_cast<int>(i)) + ":depth", {}};
        row.slice.info() = {&audioProcessor, names[static_cast<int>(i)] + " slice (sounding scene)", "s:" + juce::String(static_cast<int>(i)) + ":slice", "beats"};
        row.depth.setDoubleClickReturnValue(true, 1.0);
        row.slice.setDoubleClickReturnValue(true, 0.25);
        {
            static const char* effectInfo[] = {
                "Repeat / stutter: repeats a short slice of the sound again and again.",
                "Reverse slice: plays the previous slice backwards.",
                "Gate: chops the sound on and off in time (open for the first half of each slice, closed for the second half).",
                "Half-time: plays each slice at half speed for a slowed, heavier feel (pitch drops with it).",
                "Tape stop: slows each slice down to a stop, like a turntable or tape machine losing power.",
                "Filter sweep: a resonant low-pass filter sweeps from bright to dark across each slice and back open on the next one (DJ-style).",
                "Bit crush: gets grittier across each slice (lower sample rate and fewer bits), then resets at the next slice."};
            static_assert(std::size(effectInfo) == groovemorph::core::operationCount, "one tooltip per operation");
            row.enabled.setTooltip(juce::String(effectInfo[i]) + " Tick to switch it on for the scene being edited.");
            row.depth.setTooltip(names[static_cast<int>(i)] + " depth: how much of the effect is mixed in for the scene being edited (0 = none, 1 = full).");
        }
        row.depth.setRange(0, 1, 0.01);
        row.depth.setNumDecimalPlacesToDisplay(2);
        row.slice.setNumDecimalPlacesToDisplay(3);
        row.slice.setRange(1.0 / 16.0, 4, 1.0 / 16.0);
        row.slice.setTooltip(names[static_cast<int>(i)] + " slice in meter beats. Bounded 4-second history limits effective duration: repeat < 1.33 s, reverse < 2 s, half-time/tape stop < 4 s; gate, filter sweep and bit crush use the full requested value.");
        row.enabled.onClick = [this, i] { editOperation(i, 0); };
        row.depth.onDragStart = [this, i] { operations[i].draggingDepth = true; };
        row.slice.onDragStart = [this, i] { operations[i].draggingSlice = true; };
        row.depth.onDragEnd = [this, i] { operations[i].draggingDepth = false; editOperation(i, 1); };
        row.slice.onDragEnd = [this, i] { operations[i].draggingSlice = false; editOperation(i, 2); };
        row.depth.onValueChange = [this, i] { if (!operations[i].draggingDepth) editOperation(i, 1); };
        row.slice.onValueChange = [this, i] { if (!operations[i].draggingSlice) editOperation(i, 2); };
        addAndMakeVisible(row.enabled);
        addAndMakeVisible(row.depth);
        addAndMakeVisible(row.slice);
    }
    previewButton.setTooltip("Audition the scene being edited without saving it: you hear your edits live, and nothing changes until you press Apply preview.");
    compareButton.setTooltip("While previewing, switches between your edited version and the stored scene so you can compare them by ear.");
    applyButton.setTooltip("Keeps the previewed edit as the scene (one Undo step).");
    revertButton.setTooltip("Throws the previewed edit away and returns to the stored scene.");
    undoButton.setTooltip("Undoes the last scene edit (effects, names, MIDI notes, generated variations).");
    redoButton.setTooltip("Redoes the scene edit you just undid.");
    previewButton.onClick = [this]
    {
        editName();
        editedVariation = audioProcessor.getSceneBank().variation(editingIndex);
        audioProcessor.beginAudition(editingIndex);
        comparingStored = false;
        timerCallback();
    };
    compareButton.onClick = [this]
    {
        comparingStored = !comparingStored;
        audioProcessor.setAuditionComparingStored(comparingStored);
        timerCallback();
    };
    applyButton.onClick = [this] { finishPreview(true); };
    revertButton.onClick = [this] { finishPreview(false); };
    undoButton.onClick = [this] { audioProcessor.undoSceneEdit(); refreshSceneControls(); };
    redoButton.onClick = [this] { audioProcessor.redoSceneEdit(); refreshSceneControls(); };
    for (auto* button : {&triggerButton, &cancelButton, &previewButton, &compareButton,
                         &applyButton, &revertButton, &undoButton, &redoButton})
        addAndMakeVisible(*button);
    configureSlider(midiNoteSlider, "MIDI note for edited variation (0-127)");
    returnOnNoteOff.setTooltip("When on, letting go of a scene's MIDI note returns to the scene that was sounding before it (applies to all four scenes). When off, releasing a note does nothing.");
    midiNoteSlider.setTooltip("MIDI note (0-127; 60 = middle C) that switches to the scene being edited. Each scene has its own note.");
    midiNoteSlider.setRange(0, 127, 1);
    midiNoteSlider.setNumDecimalPlacesToDisplay(0);
    midiNoteSlider.onDragStart = [this] { draggingMidi = true; };
    midiNoteSlider.onDragEnd = [this] { draggingMidi = false; editMidiMap(); };
    // Keyboard and wheel steps pass through other scenes' notes; wait for a pause so only the note the user
    // stops on can raise the swap question. Drags and typed values still apply immediately.
    midiNoteSlider.onValueChange = [this] { if (!draggingMidi && !refreshing) midiEditDue = juce::Time::getMillisecondCounter() + 600; };
    returnOnNoteOff.onClick = [this] { editMidiMap(); };
    addAndMakeVisible(midiNoteSlider);
    addAndMakeVisible(returnOnNoteOff);
    groovemorph::ui::SceneEditAccess access;
    access.editingIndex = [this] { return editingIndex; };
    access.settings = [this] { return editedVariation.settings; };
    access.setSettings = [this](const groovemorph::core::VariationSettings& settings) { applySceneSettings(settings); };
    access.storedSettings = [this] { return audioProcessor.getSceneBank().variation(editingIndex).settings; };
    access.previewCandidate = [this](const groovemorph::core::VariationSettings& settings) { previewCandidate(settings); };
    access.previewing = [this] { return audioProcessor.isAuditioning(); };
    inspector = std::make_unique<groovemorph::ui::InspectorPanel>(audioProcessor, std::move(access));
    addAndMakeVisible(*inspector);
    presetBar.onChanged = [this] { refreshSceneControls(); };
    addAndMakeVisible(presetBar);
    easyModeButton.setTooltip("Switches to Easy mode: one compact screen with the four scenes, Surprise me and the main macros.");
    easyModeButton.setTitle("Easy mode");
    easyModeButton.setDescription("Switch to the compact Easy mode view");
    easyModeButton.onClick = [this] { applyEasyMode(true); };
    addAndMakeVisible(easyModeButton);
    easyView = std::make_unique<groovemorph::ui::EasyView>(audioProcessor);
    easyView->onOpenFullView = [this] { applyEasyMode(false); };
    addChildComponent(*easyView);
    refreshSceneControls();
    // Small displays (e.g. 1024x768): shrink the whole editor to fit instead of cropping it.
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        const float fit = juce::jmin(1.0f, static_cast<float>(area.getWidth() - 40) / 1290.0f,
                                     static_cast<float>(area.getHeight() - 100) / 784.0f);
        if (fit < 1.0f && fit > 0.0f)
            setScaleFactor(juce::jmax(0.5f, fit));
    }
    setResizable(true, true);
    setResizeLimits(1230, 784, 1900, 1100); // 784 = bottom of the preset bar (772) + margin; seven operation rows
    groovemorph::ui::finishControlMenus(*this, audioProcessor);
    setSize(1290, 784);
    applyEasyMode(audioProcessor.isEasyMode());
    startTimerHz(10);
}

GrooveMorphAudioProcessorEditor::~GrooveMorphAudioProcessorEditor()
{
    stopTimer();
    audioProcessor.cancelMidiLearn(); // a closed editor must not leave MIDI learn armed
    visualWindow.reset(); // closes the Visual window and deactivates tracing
    // A hidden preview must never remain active after the editor closes.
    if (audioProcessor.isAuditioning()) audioProcessor.revertAudition();
}

void GrooveMorphAudioProcessorEditor::refreshSceneControls()
{
    const juce::ScopedValueSetter<bool> guard(refreshing, true);
    const auto bank = audioProcessor.getSceneBank();
    if (!audioProcessor.isAuditioning()) editedVariation = bank.variation(editingIndex);
    variationName.setText(juce::String(editedVariation.name), false);
    for (std::size_t i = 0; i < operations.size(); ++i)
    {
        const auto& settings = editedVariation.settings.operations[i];
        auto& row = operations[i];
        row.enabled.setToggleState(settings.enabled, juce::dontSendNotification);
        row.depth.setValue(settings.depth, juce::dontSendNotification);
        row.slice.setValue(settings.sliceBeats, juce::dontSendNotification);
    }
    midiNoteSlider.setValue(bank.midiNoteMap().notes[editingIndex], juce::dontSendNotification);
    returnOnNoteOff.setToggleState(bank.midiNoteMap().noteOffPolicy ==
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
    const auto& row = operations[index];
    auto settings = editedVariation.settings;
    auto& op = settings.operations[index];
    if (field == 0) op.enabled = row.enabled.getToggleState();
    else if (field == 1) op.depth = static_cast<float>(row.depth.getValue());
    else op.sliceBeats = static_cast<float>(row.slice.getValue());
    applySceneSettings(settings);
}

void GrooveMorphAudioProcessorEditor::editName()
{
    if (refreshing) return;
    editedVariation.name = groovemorph::core::scene::sanitizeName(variationName.getText().toStdString());
    if (audioProcessor.isAuditioning()) audioProcessor.setAuditionEdit(editedVariation);
    else audioProcessor.setVariationName(editingIndex, editedVariation.name);
    variationName.setText(juce::String(editedVariation.name), false);
}

void GrooveMorphAudioProcessorEditor::editMidiMap()
{
    if (refreshing || audioProcessor.isAuditioning() || midiSwapDialogOpen) return;
    namespace scene = groovemorph::core::scene;
    auto map = audioProcessor.getSceneBank().midiNoteMap();
    map.noteOffPolicy = returnOnNoteOff.getToggleState()
        ? scene::NoteOffPolicy::ReturnToPrevious
        : scene::NoteOffPolicy::Ignore;
    const int requested = static_cast<int>(midiNoteSlider.getValue());
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

void GrooveMorphAudioProcessorEditor::timerCallback()
{
    if (midiEditDue != 0 && static_cast<juce::int32>(juce::Time::getMillisecondCounter() - midiEditDue) >= 0)
    {
        midiEditDue = 0;
        editMidiMap();
    }
    updateTransportButtons();
    presetBar.refresh();
    timingLabel.setText(describeTiming(audioProcessor), juce::dontSendNotification);
    const auto bank = audioProcessor.getSceneBank();
    const auto active = audioProcessor.getActiveVariation();
    const auto queued = audioProcessor.getQueuedVariation();
    for (std::size_t i = 0; i < variationButtons.size(); ++i)
    {
        juce::String text = juce::String::charToString(static_cast<juce::juce_wchar>('A' + i));
        text << ": " << juce::String(bank.variation(i).name);
        if (i == active) text << " [sounding]";
        if (static_cast<int>(i) == queued) text << " [queued]";
        if (i == editingIndex) text << " [editing]";
        variationButtons[i].setButtonText(text);
        variationButtons[i].setColour(juce::TextButton::buttonColourId,
            i == active ? juce::Colour(0xff2e7d32)
                        : static_cast<int>(i) == queued ? juce::Colour(0xff8d6e00) : juce::Colour(0xff3a4658));
        variationButtons[i].setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    }
    juce::String status = audioProcessor.isTransportPlaying() ? "Playing" : "Stopped";
    status << " | editing " << juce::String::charToString(static_cast<juce::juce_wchar>('A' + editingIndex))
           << (editingIndex == active ? " (sounding)" : " (not sounding)") << " | bar " << juce::String(audioProcessor.getBarInPhrase() + 1.0, 2)
           << " | beat " << juce::String(audioProcessor.getBeatInBar() + 1.0, 2);
    if (queued >= 0)
    {
        const auto samples = audioProcessor.getSamplesUntilSwitch();
        status << " | queued " << juce::String::charToString(static_cast<juce::juce_wchar>('A' + queued));
        if (samples >= 0) status << " in " << juce::String(samples) << " samples";
        else status << " (waiting for trigger/transport)";
    }
    const bool preview = audioProcessor.isAuditioning();
    bool draggingOperation = false;
    for (const auto& row : operations)
        draggingOperation = draggingOperation || row.draggingDepth || row.draggingSlice;
    // Host state restore can replace scene data while the editor remains open.
    if (!preview && !draggingOperation && !variationName.hasKeyboardFocus(true)
        && !(editedVariation == bank.variation(editingIndex)))
        refreshSceneControls();
    if (!preview && !draggingMidi && !midiNoteSlider.hasKeyboardFocus(true))
    {
        midiNoteSlider.setValue(bank.midiNoteMap().notes[editingIndex], juce::dontSendNotification);
        returnOnNoteOff.setToggleState(bank.midiNoteMap().noteOffPolicy ==
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
    if (juce::Time::getMillisecondCounter() < midiNoticeUntil) status << " | MIDI link table full (64): not linked";
    if (juce::Time::getMillisecondCounter() < lockNoticeUntil) status << " | locked field(s) kept (Generator tab > Locks)";
    if (preview) status << (comparingStored ? " | Preview: stored" : " | Preview: edited")
                        << (audioProcessor.isAuditionDirty() ? " (unapplied)" : "");
    const auto recovery = audioProcessor.getLastSceneLoadReport();
    if (recovery.status != groovemorph::core::scene::LoadStatus::Ok)
        status << " | Scene state recovered (see tooltip)";
    statusLabel.setText(status, juce::dontSendNotification);
    juce::String details = status;
    for (const auto& message : recovery.messages) details << "\n" << juce::String(message);
    statusLabel.setTooltip(details);
    compareButton.setButtonText(comparingStored ? "Hear edited" : "Hear stored");
    previewButton.setEnabled(!preview);
    compareButton.setEnabled(preview);
    applyButton.setEnabled(preview);
    revertButton.setEnabled(preview);
    editVariation.setEnabled(!preview);
    copyActiveFxButton.setEnabled(!preview && active != editingIndex);
    undoButton.setEnabled(!preview && audioProcessor.canUndoSceneEdit());
    redoButton.setEnabled(!preview && audioProcessor.canRedoSceneEdit());
    midiNoteSlider.setEnabled(!preview && !midiSwapDialogOpen);
    returnOnNoteOff.setEnabled(!preview && !midiSwapDialogOpen);
    cancelButton.setEnabled(queued >= 0);
    // The scheduler captures the request policy; changing the selector does not
    // rewrite it. Keep manual triggering available for any armed request.
    triggerButton.setEnabled(queued >= 0);
    if (const auto duplicateNote = bank.midiNoteMap().firstConflictingNote())
    {
        // Keep existing project maps intact, but make first-match priority visible.
        juce::String text("MIDI conflict: note ");
        text << *duplicateNote << " maps to multiple variations";
        midiLabel.setText(text, juce::dontSendNotification);
        juce::String tooltip("Note ");
        tooltip << *duplicateNote
                << " is assigned to multiple variations. The first mapped variation wins. "
                   "Set the note of one of them again (you will be offered a swap) to restore independent triggering. "
                   "Received note events: "
                << juce::String(audioProcessor.getMidiNoteEventCount());
        midiLabel.setTooltip(tooltip);
    }
    else
    {
        midiLabel.setText(audioProcessor.getMidiNoteEventCount() == 0
            ? "MIDI: no notes received; route host MIDI to GrooveMorph"
            : "MIDI: " + juce::String(audioProcessor.getMidiNoteEventCount()) + " note events received",
            juce::dontSendNotification);
        midiLabel.setTooltip("Incoming MIDI note events from the host");
    }
    demoButton.setToggleState(audioProcessor.isDemoSourceEnabled(), juce::dontSendNotification);
    if (inspector != nullptr && !refreshing) inspector->refresh();
    if (easyShown && easyView != nullptr) easyView->refresh();
    repaint();
}

void GrooveMorphAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff151b24));
    if (easyShown) return; // the Easy view covers the whole editor
    graphics.setColour(juce::Colours::white);
    const auto width = contentWidth() - 40;
    const auto column = width / 2;
    const juce::StringArray left{"Intensity", "Timing method", "Manual tempo", "Beats / beat unit"};
    const juce::StringArray right{"Dry / wet", "Transition", "Phrase bars", ""};
    for (int i = 0; i < 4; ++i)
    {
        graphics.drawText(left[i], 20, 56 + i * 38, 124, 30, juce::Justification::centredLeft);
        graphics.drawText(right[i], 20 + column, 56 + i * 38, 110, 30, juce::Justification::centredLeft);
    }
    graphics.drawText("Depth", 224, 375, (width - 204) / 2, 24, juce::Justification::centredLeft);
    graphics.drawText("Slice (meter beats)", 224 + (width - 204) / 2, 375,
                      (width - 204) / 2, 24, juce::Justification::centredLeft);
    graphics.drawText("MIDI note", 20, 670, 120, 30, juce::Justification::centredLeft);
    auto grid = juce::Rectangle<float>(20, 265, static_cast<float>(width), 26);
    graphics.setColour(juce::Colour(0xff293447));
    graphics.fillRect(grid);
    const auto bars = juce::jmax(1, static_cast<int>(phraseSlider.getValue()));
    graphics.setColour(juce::Colours::grey);
    for (int i = 0; i < bars; ++i)
        graphics.drawVerticalLine(static_cast<int>(grid.getX() + grid.getWidth() * i / bars), grid.getY(), grid.getBottom());
    if (audioProcessor.hasValidTiming())
    {
        graphics.setColour(juce::Colours::cyan);
        const auto phase = juce::jlimit(0.0, 1.0, audioProcessor.getBarInPhrase() / bars);
        graphics.fillRect(grid.getX() + static_cast<float>(phase) * (grid.getWidth() - 2), grid.getY(), 2.0f, grid.getHeight());
    }
}

void GrooveMorphAudioProcessorEditor::resized()
{
    const int width = contentWidth() - 40, column = width / 2;
    titleLabel.setBounds(20, 12, 160, 36);
    playButton.setBounds(184, 16, 62, 30);
    stopButton.setBounds(250, 16, 62, 30);
    resetButton.setBounds(322, 16, 70, 30);
    phraseStripTip.setBounds(20, 265, width, 26);
    bypassButton.setBounds(contentWidth() - 120, 16, 100, 30);
    demoButton.setBounds(contentWidth() - 240, 16, 110, 30);
    visualButton.setBounds(contentWidth() - 340, 16, 90, 30);
    if (inspector != nullptr) inspector->setBounds(contentWidth(), 8, inspectorWidth - 8, getHeight() - 16);
    presetBar.setBounds(20, 732, contentWidth() - 40, 40); // N02: preset bar in the strip under the MIDI status row (ends at 772 < minimum height 784)
    intensitySlider.setBounds(144, 56, column - 124, 30);
    dryWetSlider.setBounds(20 + column + 110, 56, column - 110, 30);
    timingMode.setBounds(144, 94, column - 134, 30);
    transitionMode.setBounds(20 + column + 110, 94, column - 110, 30);
    tempoSlider.setBounds(144, 132, column - 124, 30);
    phraseSlider.setBounds(20 + column + 110, 132, column - 110, 30);
    meterSlider.setBounds(144, 170, column - 204, 30);
    denominator.setBounds(20 + column - 76, 170, 66, 30);
    easyModeButton.setBounds(20 + column + 110, 170, column - 110, 30);
    if (easyView != nullptr) easyView->setBounds(getLocalBounds());
    timingLabel.setBounds(20, 208, width, 26);
    statusLabel.setBounds(20, 234, width, 26);
    const int buttonWidth = width / 4;
    for (std::size_t i = 0; i < variationButtons.size(); ++i)
        variationButtons[i].setBounds(20 + static_cast<int>(i) * buttonWidth, 300, buttonWidth - 6, 34);
    editVariation.setBounds(20, 342, 150, 30);
    variationName.setBounds(180, 342, 200, 30);
    // Fits the minimum 760px layout without overlapping the queued-action buttons.
    copyActiveFxButton.setBounds(387, 342, 78, 30);
    triggerButton.setBounds(contentWidth() - 290, 342, 132, 30);
    cancelButton.setBounds(contentWidth() - 150, 342, 130, 30);
    const int operationWidth = (width - 204) / 2;
    for (std::size_t i = 0; i < operations.size(); ++i)
    {
        const int y = 402 + static_cast<int>(i) * 32;
        operations[i].enabled.setBounds(20, y, 194, 28);
        operations[i].depth.setBounds(224, y, operationWidth - 10, 28);
        operations[i].slice.setBounds(224 + operationWidth, y, operationWidth - 10, 28);
    }
    int x = 20;
    for (auto* button : {&previewButton, &compareButton, &applyButton, &revertButton, &undoButton, &redoButton})
    {
        button->setBounds(x, 632, width / 6 - 6, 30);
        x += width / 6;
    }
    midiNoteSlider.setBounds(140, 670, 240, 30);
    returnOnNoteOff.setBounds(400, 670, width - 380, 30);
    midiLabel.setBounds(20, 702, width, 26);
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
    playButton.setToggleState(playing, juce::dontSendNotification);
    stopButton.setToggleState(!playing, juce::dontSendNotification);
    playButton.setEnabled(internal);
    stopButton.setEnabled(internal);
    if (!internal)
    {
        const juce::String why = "The host controls play and stop here; these buttons only show its transport state ("
                                 + juce::String(playing ? "playing" : "stopped") + "). Use the host's transport, or choose Timing method = Sample Clock without a host play state to use them.";
        playButton.setTooltip(why);
        stopButton.setTooltip(why);
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
            safe->editVariation.setSelectedId(1, juce::dontSendNotification);
            safe->demoButton.setToggleState(false, juce::dontSendNotification);
            safe->refreshSceneControls();
            safe->updateTransportButtons();
        });
}

bool GrooveMorphAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
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
    if (!key.getModifiers().isAnyModifierKeyDown())
    {
        const auto c = key.getTextCharacter();
        if (c >= '1' && c <= '4')
        {
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
