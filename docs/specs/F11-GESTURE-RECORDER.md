# F11 Gesture recording and playback

Status: IMPLEMENTED (one lane, one phrase). Beta audit: write threshold added; processor rows open (TODO B-02). Updated 2026-10-20 by the A1 completeness audit.
Replaces draft `docs/expansion/features/F11-GESTURE-RECORDER.md` (PR #6).


## Beta audit status (2026-10-20)

- Built by the audit: playback writes the target only when the value moved by more than 1/512 (`GestureLane::worthWriting`), and the DECISIONS entry that claimed playback is bracketed was corrected. Playback stays unbracketed by design.
- Open: processor tests for target change, record/play exclusivity and seek continuity (F11-T05, T07 partly, B-02); multiple lanes and higher resolution than the 20 Hz timer (F11-T08, B-06).
- Acceptance rows below carry audited statuses; the full join of ID, test, implementation and UI wiring is in [COMPLETENESS-MATRIX.md](COMPLETENESS-MATRIX.md).

## 1. Objective

Record one phrase of movement of a macro parameter and play it back by phrase position, so a filter-style ride repeats every phrase.

## 2. As implemented

- Lane: `core::control::GestureLane` (`src/core/control/GestureLane.{h,cpp}`), `binCount = 256` bins across one phrase, values normalised 0..1.
  `record(fraction, value)` writes the bin of `fraction mod 1` (non-finite fraction -> bin 0; value clamped, non-finite -> 0). `sample(fraction)`
  linearly interpolates between the nearest recorded bins in each direction with wraparound, returns `-1` when empty.
- Targets (`gestureTarget`): 0 Morph amount (`morphAmount`), 1 Intensity (`masterIntensity`), 2 Dry/Wet (`dryWet`). Changing the target clears the lane and stops recording/playback.
- Processor (`PluginProcessor.cpp`): `setGestureRecording`, `setGesturePlaying` (play only if the lane has data; recording and playing are mutually exclusive),
  `clearGesture`. `updateGesture()` runs from the 20 Hz Timer on the message thread and only while the transport plays: recording stores
  `target->getValue()` at `phraseFractionStatus` (written by the audio thread); playback calls `target->setValueNotifyingHost(gesture.sample(fraction))`
  WITHOUT holding `gestureMutex`. Resolution is therefore about 20 updates per second; for scale, one bin is 31 ms for an 8 s phrase (4 bars of 4 beats at 120 BPM), so playback and recording are coarser than the lane's storage.
- Serialization: `GestureLane::serialize()` emits space-separated `bin:value` pairs, value fixed to five decimals; `deserialize()` rejects text over 16384 bytes, skips
  malformed items, bins outside 0..255 and values that fail the fixed-decimal parser, and rebuilds through `record`.
- Project state child `Gesture`: properties `target` (int, clamped 0..2 on load) and `bins` (the text above). Recording/playing flags are NOT restored (restored stopped).

## 3. Parameters

No new host parameters. Gesture controls are not automatable. Playback writes to existing parameters.

## 4. State keys

`Gesture` child: `target`, `bins`. Absent in older projects -> empty lane, target 0.

## 5. Real-time and threading

All gesture logic runs on the message thread (Timer); the audio thread never touches the lane. `gestureMutex` guards lane/flags; host notification is made outside it.
KNOWN DISCREPANCY (owned by W10): `docs/coordination/DECISIONS.md` says gesture playback is bracketed with `beginChangeGesture/endChangeGesture`; the code comment and implementation
deliberately do NOT bracket playback (plugin-driven stream, not a user touch). Target: keep playback unbracketed, correct the DECISIONS entry, and make playback write only when the
sampled value differs from the parameter by more than 1/512 so a host in write mode is not flooded with identical automation points.

## 6. UI wiring

Perform tab: "Gesture lane (records one phrase of movement)", target combo (Morph amount, Intensity, Dry / wet), "Record", "Play", "Clear", status label (filled bins).

## 7. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F11-T01 | Empty lane returns -1; exact bin; interpolation up/down; wrap; clamped and wrapped inputs; clear | EXISTS | `tests/core/LoopSourceTests.cpp` (lane cases are hosted in this file: "empty lane", "two bins", "exact bin", "interpolates up", "wraps back down", "clamped and wrapped", "cleared") |
| F11-T02 | Serialization restores every bin; hostile text skipped; oversized text ignored | EXISTS | `tests/core/SceneControlTests.cpp` "gesture restores every bin", "hostile gesture text is skipped, not trusted", "oversized gesture text is ignored" |
| F11-T03 | Record then play back the recorded value; clear | EXISTS | `tests/PluginProcessorTests.cpp` "gesture recorded", "gesture plays back the recorded value", "gesture cleared" |
| F11-T04 | Target and bins restored from a project; legacy project without `Gesture` loads | PARTIAL | `PluginProcessorTests.cpp` "gesture target restored" Target restore is asserted; a dedicated legacy-project-without-Gesture fixture is not. |
| F11-T05 | Changing target clears the lane; record and play are exclusive | DEFERRED | Processor-level test needs the JUCE build, which the core-only authoring machine cannot run; open as TODO B-02. |
| F11-T06 | Playback does not write the parameter when stopped or when the sampled value is within 1/512 (target 5) | DEFERRED | Processor-level test needs the JUCE build, which the core-only authoring machine cannot run; open as TODO B-02. |
| F11-T07 | Playback follows phrase wrap and seek (phrase fraction continuity) | PARTIAL | `LoopSourceTests.cpp` "wraps back down"; `PluginProcessorTests.cpp` "gesture plays back the recorded value" Phrase-fraction wrap and interpolation of the lane are tested at core level; seek continuity in the processor is not. |
| F11-T08 | Higher resolution than the 20 Hz timer; multiple lanes | DEFERRED | Higher resolution than the 20 Hz timer and multiple lanes are not built. Open as TODO B-06. |

## 8. Gaps

W10 (host automation / timer-thread safety): section 5 target and a test that nothing in this path runs on the audio thread. Unowned, deferred: multiple lanes, speed/loop/quantize options, overdub, audio-thread
sample-accurate playback, recording the scene settings. Playback priority against host automation: today the last writer wins per update; the user must not record automation on the same parameter while a gesture plays (documented in help).

## 9. Non-goals

A general automation editor, MIDI-clip style editing, per-event gestures.
