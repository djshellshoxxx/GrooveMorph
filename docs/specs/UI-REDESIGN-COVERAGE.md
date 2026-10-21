# UI-REDESIGN coverage: old editor controls vs the Studio shell (UR-02)

Scope: everything the old `GrooveMorphAudioProcessorEditor` owned as a direct child (the 470 px inspector, `EasyView`
and `PresetBar` internals are unchanged and keep their own controls). "Name" is the component name or button text.
Nothing is dropped; the controls moved into the shell components in `src/ui/shell/`.

| # | Old control (name / text) | Old kind | New home | Same wiring |
|---|---|---|---|---|
| 1 | GrooveMorph title | Label | `HeaderBar::wordmark` (hidden below 1100 px wide) | static text |
| 2 | Intensity (0 = dry) | MenuSlider, param `masterIntensity` | `PerformanceBand::intensityKnob` (rotary, caption "Intensity") | SliderAttachment, `p:masterIntensity`, 2 decimals |
| 3 | Dry / wet | MenuSlider, `dryWet` | `PerformanceBand::dryWetKnob` | SliderAttachment, `p:dryWet` |
| 4 | Manual tempo (BPM) | MenuSlider, `manualTempo` | `TimingPanel::tempoSlider` (Timing... popover) | SliderAttachment, `p:manualTempo`, unit BPM |
| 5 | Manual beats per bar | MenuSlider, `manualMeterNumerator` | `TimingPanel::meterSlider` | SliderAttachment, `p:manualMeterNumerator` |
| 6 | Manual beat unit | MenuComboBox, `manualMeterDenominator` | `TimingPanel::denominator` | ComboBoxAttachment, `p:manualMeterDenominator` |
| 7 | Timing method | MenuComboBox, `timingMode` | `TimingPanel::timingMode` (name kept) | ComboBoxAttachment, `p:timingMode` |
| 8 | Phrase length (bars) | MenuSlider, `phraseBars` | `TimingPanel::phraseSlider` | SliderAttachment, `p:phraseBars` |
| 9 | Transition | MenuComboBox, `transitionMode` | `PerformanceBand::transitionMode` | ComboBoxAttachment, `p:transitionMode` |
| 10 | Bypass | MenuToggleButton, `bypass` | `HeaderBar::bypassButton` | ButtonAttachment, `p:bypass` |
| 11 | Demo loop | MenuToggleButton | Settings (gear) menu item "Demo loop" (ticked) | `setDemoSourceEnabled`; tick read from `isDemoSourceEnabled()` |
| 12 | Visual | MenuToggleButton | `HeaderBar::visualButton` | same VisualWindow open/close/on-close callback |
| 13 | Play | TextButton | `HeaderBar::playButton` | `startInternalTransport`, enabled only for internal transport |
| 14 | Stop | TextButton | `HeaderBar::stopButton` | `stopInternalTransport` |
| 15 | Reset | TextButton | Settings menu item "Reset to defaults..." | same confirm AlertWindow, `resetToDefaults` |
| 16 | Easy mode | TextButton | `HeaderBar::easyButton` (text "Easy", title "Easy mode") | `applyEasyMode(true)` |
| 17 | Preset bar | PresetBar | `HeaderBar::presetBar` | `onChanged` -> `refreshSceneControls`, `refresh()` from the timer |
| 18 | Phrase strip (tip area) | TipArea + painted cursor | `TimingStrip` (painted bars + cursor, tip area over it) | `phraseBars` + `getBarInPhrase`, 30 Hz dirty-rect repaint |
| 19 | timingLabel | Label | `TimingStrip::timingLabel` | `describeTiming` text, also its tooltip |
| 20 | statusLabel | Label | `TimingStrip::statusLabel` | same status text, recovery messages in tooltip |
| 21 | midiLabel | Label | `FooterBar::midiLabel` | same MIDI conflict / event-count text |
| 22-25 | Scene buttons A-D | 4 TextButtons | `PerformanceBand::pads[0..3]` (`ScenePad`) | `requestVariation(i)` (now also `cancelSurprise()` first, as Easy mode does); text "A: name [sounding] [queued] [editing]" kept as button text |
| 26 | Trigger queued | TextButton | `PerformanceBand::triggerButton` | `triggerManualSwitch`, enabled while queued |
| 27 | Cancel queued | TextButton | `PerformanceBand::cancelButton` | `cancelQueuedSwitch`, enabled while queued |
| 28 | Variation to edit | MenuComboBox | `EffectsRack::editVariation` | locked during preview, same refresh |
| 29 | Variation name | TextEditor | `EffectsRack::variationName` | `editName` on return and focus lost, max length |
| 30 | Copy FX | TextButton | `EffectsRack::copyActiveFxButton` | `copyVariationSettings(active, editing)` |
| 31-33 | Repeat / stutter: Enable, Depth, Slice | MenuToggleButton + 2 MenuSlider | `EffectsRack::cards[0]` (`EffectCard`) | `editOperation`, `s:0:enabled/depth/slice`, double-click 1.0 / 0.25, drag start/end logic |
| 34-36 | Reverse slice | same | `cards[1]`, `s:1:*` | same |
| 37-39 | Gate | same (toggle text "Gate") | `cards[2]`, `s:2:*` | same |
| 40-42 | Half-time | same | `cards[3]`, `s:3:*` | same |
| 43-45 | Tape stop | same | `cards[4]`, `s:4:*` | same |
| 46-48 | Filter sweep | same | `cards[5]`, `s:5:*` | same |
| 49-51 | Bit crush | same | `cards[6]`, `s:6:*` | same |
| 52 | Preview | TextButton | `EffectsRack::previewButton` | `beginAudition` flow |
| 53 | Hear stored / Hear edited | TextButton | `EffectsRack::compareButton` | `setAuditionComparingStored` |
| 54 | Apply preview | TextButton | `EffectsRack::applyButton` | `finishPreview(true)` |
| 55 | Revert preview | TextButton | `EffectsRack::revertButton` | `finishPreview(false)` |
| 56 | Undo | TextButton | `PerformanceBand::undoButton` | cancels a pending Surprise first, else `undoSceneEdit`; also Ctrl/Cmd+Z |
| 57 | Redo | TextButton | `PerformanceBand::redoButton` | `redoSceneEdit`; also Ctrl/Cmd+Shift+Z, Ctrl+Y |
| 58 | MIDI note for edited variation | MenuSlider | `FooterBar::midiNoteSlider` | drag/typed/paused-step logic, A04 swap AlertWindow unchanged |
| 59 | Return on MIDI note-off | MenuToggleButton | `FooterBar::returnOnNoteOff` | `editMidiMap` |
| 60 | Inspector | InspectorPanel | central workspace (all remaining space) | constructed with the same `SceneEditAccess` |
| 61 | Easy view | EasyView | unchanged full-window overlay | `applyEasyMode` hides every visible child and restores it |

New controls (not in the old editor): Morph knob (`morphAmount`) and Chaos hero knob (`chaos`), Morph target combo
(`morphTarget`), Surprise me, Settings gear menu (Demo loop, Reset to defaults, UI scale 75/100/125/150/200, Show
guide, Help), Timing... button, Close button of the timing panel, footer hint line, F1 help popover.

## Behaviour kept

- Timers: 10 Hz state timer (all old work), plus a 30 Hz timer that repaints only the phrase cursor columns and
  queued scene pads.
- Shortcuts: Space, 1-4, T, Esc kept; added Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z, Ctrl+Y, F1. The editor now wants
  keyboard focus and takes it when empty space is clicked.
- Easy mode overlay, Visual window toggle and callback, preview/audition flow, A04 note-swap dialog, reset confirm,
  MIDI link table full alert, lock notice, scene-recovery tooltip: unchanged logic.
- Small-display fit (`setScaleFactor`) now computed against the 1180x760 default and multiplied by the UI scale.

## Differences to review

- Demo loop and Reset are menu items instead of always visible buttons.
- While a Surprise is pending the effect cards, name field and Preview/Hear/Apply/Revert buttons are disabled so
  they cannot overwrite the candidate; Undo cancels the Surprise (same as Easy mode), and it is kept at the next phrase.
- The Timing popover is an in-window overlay (a child of the editor, so the UI-AX-1 walk and tests reach its
  controls): closes with its Close button, Esc, or a click on empty editor space.
