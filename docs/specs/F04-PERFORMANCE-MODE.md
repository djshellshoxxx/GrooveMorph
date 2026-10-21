# F04 Live performance mode

Status: IMPLEMENTED (Perform tab and MIDI macros), spec COMPLETE with W10 targets. Written 2026-10-20 against `c83e8b2`.
Replaces draft `docs/expansion/features/F04-PERFORMANCE-MODE.md` (PR #6). The draft's priority queue, Hold Fill and Return to Base
are replaced by the existing Scheduler plus C05 automatic return (section 7).

## 1. Objective

Time-critical actions in one place: launch scenes quantized, trigger or cancel a queued switch, ride Intensity, Dry/Wet and Morph,
all reachable from MIDI.

## 2. As implemented

- Scene launch: `GrooveMorphAudioProcessor::requestVariation(index)` stores `uiRequest` (atomic); the audio thread calls
  `scheduler.request(index, transitionMode)` at the next block start and disarms any pending automatic return (C05).
- Transition quantization: host parameter `transitionMode` (Choice: Immediate, Next Beat, Next Bar, Next Phrase, Manual; default Next Bar).
  `Scheduler` (`src/core/Scheduler.cpp`) fires quantized modes only while the transport plays; Manual fires on `trigger()`.
- Trigger/Cancel: `triggerManualSwitch()` -> `uiTrigger`, `cancelQueuedSwitch()` -> `uiCancel` (also disarms C05).
- Macros: parameters `masterIntensity` (0..1, default 0.5), `dryWet` (0..1, default 1.0), `morphAmount`. `wetAmount = intensity * dryWet`.
- Bypass: host parameter `bypass` (bool); `getBypassParameter()` returns it; 10 ms linear fade (`bypassFadeSeconds`) to the exact dry buffer
  (acts as the "emergency dry").
- MIDI notes: `MidiNoteMap` (default C3=60..D#3=63 -> scenes A..D), `NoteOffPolicy::Ignore|ReturnToPrevious`, handled in `handleMidi`
  with the audio-thread `MidiTriggerState`; request uses the current transition mode.
- MIDI macros (opt-in, `setMidiMacrosEnabled`, NOT saved): CC 20/21/22 -> Intensity/Dry-Wet/Morph amount; program change 0-3 -> scene.
  The audio thread stores the CC value in an atomic and the 20 Hz processor timer applies it to the parameter bracketed with
  `beginChangeGesture/endChangeGesture` (`applyMidiMacros`). Effect: macro CC changes reach the engine up to about 50 ms late.
- Learnable CC links: see `core/MidiLinks.h` (`p:<paramId>` and `s:<effect>:<depth|slice|enabled>` targets, 64 links, serialized in state tree `MidiLinks`).

## 3. Parameters

All existing, listed above; no new IDs. Version hint 1 everywhere.

## 4. State

Parameters in APVTS; note map in scene keys `midi.note.<i>`, `midi.noteoff`; links in `MidiLinks` child property `data`
(`"20=p:masterIntensity;21=s:0:depth"`). Macro-enable and source mode are session state (not saved).

## 5. Real-time and threading

UI -> audio through single-slot atomics (latest request wins; `exchange(-1)` consume). MIDI handling is allocation-free. Host and CC writes
are made on the message thread by the timer. Known weakness (owned by W10): CC macro/link application and gesture playback run on
the JUCE Timer (message thread) and write host parameters; confirm no path calls the host from the audio thread and add a test hook.

## 6. UI wiring

Perform tab (`PerformTab`): scene buttons A-D, Transition combo, "Trigger queued", "Cancel queued", three macro sliders attached to
`masterIntensity`, `dryWet`, `morphAmount`, a morph-target combo, Gesture and Evolution sections (F11, F05), phase/restart controls (C04).
Main editor: scene row, queued/sounding indicators, MIDI note per scene, "return on note-off".

## 7. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F04-T01 | Request queues, Next Bar lands sample-accurately, mid-block and with MIDI splits | EXISTS | `tests/PluginProcessorTests.cpp` "param change queues", "switch on bar", "MIDI sub-block PPQ switch at actual bar", "PPQ rendering independent of irrelevant MIDI splits" |
| F04-T02 | Manual mode waits for trigger; cancel clears the queue and realigns the host parameter | EXISTS | PluginProcessorTests "manual queued", "manual trigger", "queue cleared", "cancel clears queue", "cancel realigns host parameter to sounding variation" |
| F04-T03 | MIDI note-on maps/queues; note-off ignored by default, returns when configured | EXISTS | `tests/core/SceneBankTests.cpp` "note-on maps", "note-off returns"; PluginProcessorTests "midi immediate at offset", "midi queues next bar", "note-off ignored by default" |
| F04-T04 | Macros off by default; CC 20/21/22 and program change behave when on | EXISTS | PluginProcessorTests "macros off by default: CC and program change ignored", "CC 20 sets intensity", "CC 22 sets morph amount", "program change selects a scene" |
| F04-T05 | Bypass reaches exact dry and ramps click-free | EXISTS | PluginProcessorTests "bypass reaches exact dry", "bypass ramps", "bypass click-free" |
| F04-T06 | Learnable links (learn, relink, bounds, serialization) | EXISTS | `tests/core/MidiLinksTests.cpp` (all cases); processor cases "learning", "first moving controller is linked" |
| F04-T07 | Scene keyboard shortcuts (target section 8): keys 1-4 request scenes A-D, Esc cancels queued switch, T triggers | MISSING, owned by W10 | `tests/PluginProcessorTests.cpp` editor section |
| F04-T08 | Large-format layout renders without clipping at 1230x700 minimum | MISSING (needs native UI run) | manual QA item |
| F04-T09 | Macro CC application never calls the host from the audio thread | MISSING, owned by W10 | thread-assert test |

## 8. Gaps and targets

- W10: keyboard control of the editor (scene select, trigger, cancel, transport already on Space in `PluginEditor::keyPressed`),
  MIDI conflict policy (see [A04](A04-MIDI-CONFLICT-POLICY.md)), accessibility ([A07](A07-ACCESSIBILITY-INPUT.md)), timer-thread safety.
- Unowned/deferred: draft "Hold Fill" and "Return to Base" are served by C05 (`autoReturnBars`, target Previous); no momentary latch exists.
- Dedicated large-format layout is deferred (Perform is a tab).

## 9. Non-goals

A second scheduler, a general command queue, or a custom macro-mapping panel (the right-click MIDI learn menu is the mapping UI).
