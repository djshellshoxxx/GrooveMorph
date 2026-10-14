#include "PluginEditor.h"
#include "PluginProcessor.h"

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

void GrooveMorphAudioProcessorEditor::addSlider(juce::Slider& slider,
    const juce::String& parameter, const juce::String& name)
{
    configureSlider(slider, name);
    addAndMakeVisible(slider);
    sliderAttachments.push_back(std::make_unique<SliderAttachment>(audioProcessor.parameters, parameter, slider));
}

void GrooveMorphAudioProcessorEditor::addCombo(juce::ComboBox& combo,
    const juce::String& parameter, const juce::StringArray& choices, const juce::String& name)
{
    combo.addItemList(choices, 1);
    combo.setName(name);
    combo.setTooltip(name);
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
    addAndMakeVisible(bypassButton);
    demoButton.setTooltip("Standalone evaluation aid: replaces the audio input with a built-in drum loop "
                          "(kick, snare, hat) while the transport runs, so the effects can be heard without "
                          "external audio. Not saved with the project. Use headphones; the level is moderate.");
    demoButton.onClick = [this] { audioProcessor.setDemoSourceEnabled(demoButton.getToggleState()); };
    addAndMakeVisible(demoButton);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "bypass", bypassButton);

    for (std::size_t i = 0; i < variationButtons.size(); ++i)
    {
        auto& button = variationButtons[i];
        button.onClick = [this, i] { audioProcessor.requestVariation(i); };
        button.setTooltip("Queue variation using the selected transition policy");
        addAndMakeVisible(button);
    }
    triggerButton.onClick = [this] { audioProcessor.triggerManualSwitch(); };
    cancelButton.onClick = [this] { audioProcessor.cancelQueuedSwitch(); };
    editVariation.addItemList({"Edit A", "Edit B", "Edit C", "Edit D"}, 1);
    editVariation.setSelectedId(1, juce::dontSendNotification);
    editVariation.setName("Variation to edit");
    editVariation.onChange = [this]
    {
        if (refreshing) return;
        // Pending preview edits must be explicitly applied or reverted before changing target.
        if (audioProcessor.isAuditioning())
        {
            editVariation.setSelectedId(static_cast<int>(editingIndex) + 1, juce::dontSendNotification);
            return;
        }
        editingIndex = static_cast<std::size_t>(editVariation.getSelectedId() - 1);
        refreshSceneControls();
    };
    addAndMakeVisible(editVariation);
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
    const juce::StringArray names{"Repeat / stutter", "Reverse slice", "Gate", "Half-time", "Tape stop"};
    for (std::size_t i = 0; i < operations.size(); ++i)
    {
        auto& row = operations[i];
        row.enabled.setButtonText(names[static_cast<int>(i)]);
        configureSlider(row.depth, names[static_cast<int>(i)] + " depth");
        configureSlider(row.slice, names[static_cast<int>(i)] + " slice (meter beats)");
        row.depth.setRange(0, 1, 0.01);
        row.depth.setNumDecimalPlacesToDisplay(2);
        row.slice.setNumDecimalPlacesToDisplay(3);
        row.slice.setRange(1.0 / 16.0, 4, 1.0 / 16.0);
        row.slice.setTooltip(names[static_cast<int>(i)] + " slice in meter beats. Bounded 4-second history limits effective duration: repeat < 1.33 s, reverse < 2 s, half-time/tape stop < 4 s; gate uses the full requested value.");
        row.enabled.onClick = [this, i] { editOperation(i); };
        row.depth.onDragStart = [this, i] { operations[i].draggingDepth = true; };
        row.slice.onDragStart = [this, i] { operations[i].draggingSlice = true; };
        row.depth.onDragEnd = [this, i] { operations[i].draggingDepth = false; editOperation(i); };
        row.slice.onDragEnd = [this, i] { operations[i].draggingSlice = false; editOperation(i); };
        row.depth.onValueChange = [this, i] { if (!operations[i].draggingDepth) editOperation(i); };
        row.slice.onValueChange = [this, i] { if (!operations[i].draggingSlice) editOperation(i); };
        addAndMakeVisible(row.enabled);
        addAndMakeVisible(row.depth);
        addAndMakeVisible(row.slice);
    }
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
    midiNoteSlider.setRange(0, 127, 1);
    midiNoteSlider.setNumDecimalPlacesToDisplay(0);
    midiNoteSlider.onDragStart = [this] { draggingMidi = true; };
    midiNoteSlider.onDragEnd = [this] { draggingMidi = false; editMidiMap(); };
    midiNoteSlider.onValueChange = [this] { if (!draggingMidi) editMidiMap(); };
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
    refreshSceneControls();
    setResizable(true, true);
    setResizeLimits(1180, 700, 1900, 1100);
    setSize(1240, 740);
    startTimerHz(10);
}

GrooveMorphAudioProcessorEditor::~GrooveMorphAudioProcessorEditor()
{
    stopTimer();
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

void GrooveMorphAudioProcessorEditor::applySceneSettings(const groovemorph::core::VariationSettings& settings)
{
    if (refreshing) return;
    editedVariation.settings = settings;
    if (audioProcessor.isAuditioning()) audioProcessor.setAuditionEdit(editedVariation);
    else audioProcessor.setVariationSettings(editingIndex, settings);
    refreshSceneControls();
}

void GrooveMorphAudioProcessorEditor::previewCandidate(const groovemorph::core::VariationSettings& settings)
{
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

void GrooveMorphAudioProcessorEditor::editOperation(std::size_t index)
{
    if (refreshing) return;
    const auto& row = operations[index];
    auto& settings = editedVariation.settings.operations[index];
    settings = {row.enabled.getToggleState(), static_cast<float>(row.depth.getValue()),
                static_cast<float>(row.slice.getValue())};
    if (audioProcessor.isAuditioning()) audioProcessor.setAuditionEdit(editedVariation);
    else audioProcessor.setOperationSettings(editingIndex,
        static_cast<groovemorph::core::OperationType>(index), settings);
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
    if (refreshing || audioProcessor.isAuditioning()) return;
    auto map = audioProcessor.getSceneBank().midiNoteMap();
    map.notes[editingIndex] = static_cast<int>(midiNoteSlider.getValue());
    map.noteOffPolicy = returnOnNoteOff.getToggleState()
        ? groovemorph::core::scene::NoteOffPolicy::ReturnToPrevious
        : groovemorph::core::scene::NoteOffPolicy::Ignore;
    audioProcessor.setMidiNoteMap(map);
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
    midiNoteSlider.setEnabled(!preview);
    returnOnNoteOff.setEnabled(!preview);
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
                   "Edit one variation's MIDI note to restore independent triggering. "
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
    if (inspector != nullptr && !refreshing) inspector->refresh();
    repaint();
}

void GrooveMorphAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff151b24));
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
    graphics.drawText("MIDI note", 20, 626, 120, 30, juce::Justification::centredLeft);
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
    titleLabel.setBounds(20, 12, width - 240, 36);
    bypassButton.setBounds(contentWidth() - 120, 16, 100, 30);
    demoButton.setBounds(contentWidth() - 240, 16, 110, 30);
    if (inspector != nullptr) inspector->setBounds(contentWidth(), 8, inspectorWidth - 8, getHeight() - 16);
    intensitySlider.setBounds(144, 56, column - 124, 30);
    dryWetSlider.setBounds(20 + column + 110, 56, column - 110, 30);
    timingMode.setBounds(144, 94, column - 134, 30);
    transitionMode.setBounds(20 + column + 110, 94, column - 110, 30);
    tempoSlider.setBounds(144, 132, column - 124, 30);
    phraseSlider.setBounds(20 + column + 110, 132, column - 110, 30);
    meterSlider.setBounds(144, 170, column - 204, 30);
    denominator.setBounds(20 + column - 76, 170, 66, 30);
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
        const int y = 402 + static_cast<int>(i) * 36;
        operations[i].enabled.setBounds(20, y, 194, 30);
        operations[i].depth.setBounds(224, y, operationWidth - 10, 30);
        operations[i].slice.setBounds(224 + operationWidth, y, operationWidth - 10, 30);
    }
    int x = 20;
    for (auto* button : {&previewButton, &compareButton, &applyButton, &revertButton, &undoButton, &redoButton})
    {
        button->setBounds(x, 588, width / 6 - 6, 30);
        x += width / 6;
    }
    midiNoteSlider.setBounds(140, 626, 240, 30);
    returnOnNoteOff.setBounds(400, 626, width - 380, 30);
    midiLabel.setBounds(20, 658, width, 26);
}

juce::AudioProcessorEditor* GrooveMorphAudioProcessor::createEditor()
{
    return new GrooveMorphAudioProcessorEditor(*this);
}
