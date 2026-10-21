# F12 MIDI control, plugin formats and platforms

Status: PARTIALLY IMPLEMENTED (MIDI control, VST3 + Standalone), spec COMPLETE with W9 and W10 targets. Written 2026-10-20 against `c83e8b2`.
Replaces draft `docs/expansion/features/F12-MIDI-CLAP-PLATFORMS.md` (PR #6). Related: [A04](A04-MIDI-CONFLICT-POLICY.md), [A06](A06-AUTOMATION-IDS-AND-FADE.md),
[N06](N06-CLAP-AND-RELEASE.md).

## 1. Objective

Control GrooveMorph from MIDI and automation reliably, and ship the same DSP core in every supported plugin format and platform.

## 2. As implemented

### 2.1 MIDI input
`NEEDS_MIDI_INPUT TRUE`, `acceptsMidi()` true, `producesMidi()` false (`CMakeLists.txt`, `PluginProcessor.h`). `handleMidi` (audio thread, no allocation):
- Notes: `MidiNoteMap` (4 notes, default 60..63, `NoteOffPolicy`), first matching variation wins; duplicates are only diagnosed (`firstConflictingNote()`, shown as the editor "MIDI conflict" label).
- Control change: every CC is pushed into `CcInbox` (128 atomic slots, `core/MidiLinks.h`); the 20 Hz Timer collects changes and applies them on the message thread.
- Macros (opt-in, session-only): CC 20/21/22 -> `masterIntensity`/`dryWet`/`morphAmount`; program change 0..3 -> scene request with the current transition mode.
- MIDI learn / links (`MidiLinkTable`, max 64): target strings `p:<parameterId>` (host parameter) or `s:<effect 0-4>:<depth|slice|enabled>` (sounding scene, no undo entry per move,
  locks enforced, refused while auditioning). One target has at most one CC; one CC may drive several targets. Serialized `"20=p:masterIntensity;21=s:0:depth"` in state child `MidiLinks`.
  Learn: the first CC that moves after `beginMidiLearn(target)` is linked and not applied.
- All parameter writes from CC are bracketed with `beginChangeGesture/endChangeGesture`.
- No soft pickup (value jump on first movement): CC values are applied absolutely.

### 2.2 Formats and platforms
Formats `VST3 Standalone` (`CMakeLists.txt`). Windows x64 and Linux x86_64 artifacts and a macOS arm64 + Intel alpha workflow exist (`.github/workflows/windows-alpha.yml`,
`macos-alpha.yml`, `build.yml`); CI runs headless pluginval (strictness 5) on Windows/Linux (`pluginval.yml`). Version `0.1.0`, manufacturer code `Djsl`, plugin code `Gm01`.
CLAP is not built. Standalone ignores the wrapper's transport and free-runs the Sample Clock (see TIMING help). State is shared across formats because `getStateInformation`
is format independent.

## 3. Parameters

All host parameters (stable IDs, never reused): `masterIntensity`, `manualTempo`, `manualMeterNumerator`, `manualMeterDenominator`, `phraseBars`, `dryWet`, `activeVariation`,
`transitionMode`, `bypass`, `timingMode`, `morphTarget`, `morphAmount`, `autoReturnBars`, `autoReturnTarget`, `autoReturnUnit`, `bandMode`, `bandCrossover`, `bandSlope`,
`audioTrigger`, `audioTriggerSens`. All use ParameterID version hint 1. New parameters (W2/W3/W8 and others) are appended after `audioTriggerSens` and listed in
[docs/specs/README.md](README.md) once their specs land; nobody reorders existing IDs.

## 4. State keys

Project state root `GrooveMorphState` (APVTS) with children `SceneBank` (text property `data`, `groovemorph-scenes v1`), `Gesture`, `Evolution`, `MidiLinks`. Envelope limit on load: 1 MiB
(`setStateInformation`); malformed or oversized state is ignored and the current state kept. W4 may raise the limit for a `Loop` child (see F09).

## 5. Real-time and threading

MIDI parsing on the audio thread is allocation-free; CC application and learn run on the message thread Timer. Host parameter writes are made only on the message thread.
`resetToDefaults()` and `setStateInformation` run on the message thread.

## 6. UI wiring

Right-click menu on every `MenuSlider/MenuComboBox/MenuToggleButton` (`src/ui/ControlMenu.{h,cpp}`): enter exact value, copy, paste, reset, "Link to MIDI controller", unlink; a
badge overlay shows linked controls and the learning state. Input tab: "MIDI macros: CC 20/21/22 and program change" toggle. Editor: MIDI note per scene, "Return on note-off", MIDI event counter and conflict label.

## 7. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F12-T01 | Note map: defaults, unmapped, clamping, note-off policy, collision diagnostics | EXISTS | `tests/core/SceneBankTests.cpp` "default map", "map clamped", "detect colliding MIDI note", "report first collision deterministically" |
| F12-T02 | Link table: bounds, relink, one CC many targets, serialization, hostile text | EXISTS | `tests/core/MidiLinksTests.cpp` |
| F12-T03 | CC inbox: changes reported once, discard, masking, out-of-range | EXISTS | MidiLinksTests "unchanged is not repeated", "discard forgets pending", "value masked to 7 bits" |
| F12-T04 | Processor learn: first moving CC is linked, old movement ignored, linked CC drives the parameter | EXISTS | `tests/PluginProcessorTests.cpp` "learning", "old movement does not complete learning", "first moving controller is linked" |
| F12-T05 | Macros and program change | EXISTS | PluginProcessorTests "CC 20 sets intensity", "program change selects a scene" |
| F12-T06 | MIDI sub-block timing and PPQ independence | EXISTS | PluginProcessorTests "midi immediate at offset", "MIDI sub-block PPQ switch at actual bar" |
| F12-T07 | State survives format change: a state saved by the VST3 wrapper loads in Standalone and CLAP | MISSING, owned by W9 | CLAP smoke + state fixture |
| F12-T08 | CLAP build: exposes the same parameter IDs and state; passes clap-validator | MISSING, owned by W9 | see N06 |
| F12-T09 | Stable host-automation IDs for per-operation scene fields (A06) | MISSING, owned by W10 | see A06 |
| F12-T10 | MIDI duplicate-assignment policy (A04) incl. CC link conflicts and note/CC overlap | MISSING, owned by W10 | see A04 |
| F12-T11 | Soft pickup option for CC links (target 8) | MISSING (feature absent) | deferred unless W10 adds it |
| F12-T12 | pluginval strictness 5 on VST3 after every parameter addition | EXISTS in CI (workflow) | `.github/workflows/pluginval.yml`; not a local test |
| F12-T13 | Native host automation write/read of every parameter (REAPER on Windows/Linux/macOS) | MISSING, requires native host QA (issue #19) | manual QA ledger |

## 8. Gaps and targets

- W9: CLAP adapter and release packaging ([N06](N06-CLAP-AND-RELEASE.md)); the DSP core and parameter IDs must be byte-for-byte shared with VST3. No format-specific behavior in `src/core`.
- W10: host-automation IDs (A06), MIDI duplicate policy (A04), keyboard event creation, accessibility (A07), timer-thread safety, conflict badge.
- Target soft pickup (optional, W10 discretion): when a link is first created or the scene/parameter changed by other means, ignore the CC until it crosses the current normalised value
  (within 2/127), then follow; per-link, not persisted.
- Deferred: MIDI output, MPE, mapping of aftertouch/pitch bend, OSC.

## 9. Non-goals

AU and AAX formats, mobile, a second plugin core per format.
