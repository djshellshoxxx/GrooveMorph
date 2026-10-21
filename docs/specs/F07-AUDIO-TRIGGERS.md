# F07 Audio-triggered scene actions

Status: IMPLEMENTED (transient detector to scene action). Beta audit: tuning on real material open (TODO B-07). Updated 2026-10-20 by the A1 completeness audit.
Replaces draft `docs/expansion/features/F07-AUDIO-TRIGGERS.md` (PR #6). Sidechain input, per-band detection and calibrated confidence from the
draft are NOT implemented (section 8).


## Beta audit status (2026-10-20)

- Added by the audit: sensitivity monotonic (F07-T06) and 120 ms refractory (F07-T07) in `OnsetDetectorTests.cpp`.
- Open: queued-mode and C05 interaction processor tests (F07-T05, T10, B-02); rendered drum/bass/dense fixtures with a measured retrigger rate (F07-T09, B-07).
- Acceptance rows below carry audited statuses; the full join of ID, test, implementation and UI wiring is in [COMPLETENESS-MATRIX.md](COMPLETENESS-MATRIX.md).

## 1. Objective

Let a transient in the plugin's own input request the next scene or fire an already queued transition, so the track can react to a drum hit.

## 2. As implemented

Detector (`src/core/dsp/OnsetDetector.{h,cpp}`), one instance per channel (`onsetDetectors[2]`), prepared in `prepareToPlay`:

- Fast envelope follower of `|x|`: attack 0.5 ms, release 10 ms. Slow follower: attack 20 ms, release 200 ms.
- Onset when `fast > floor(0.01)` and `fast > slow * ratio`, `ratio = 3.0 - 1.8 * sensitivity` (sensitivity 0..1, non-finite -> 0.5; so 3.0 hardest,
  1.2 easiest), detector armed, refractory expired.
- Refractory 120 ms after an onset. Warm-up 50 ms after `reset()`; the detector must see `fast < slow*1.1 + 0.005` before it arms again.
- Non-finite samples count as 0. No allocation, `noexcept`.

Processor (`PluginProcessor.cpp`):
- Parameters `audioTrigger` (Choice: Off, Next scene, Trigger queued; default Off) and `audioTriggerSens` (0..1, default 0.5).
- `renderSegment` runs detection on the segment AFTER the demo/loop source has overwritten the input (so detection hears the source) and BEFORE the band split.
  A hit sets `onsetThisBlock`.
- The reaction happens at the START of the NEXT `process()` call (one block of latency): mode 1 calls
  `scheduler.request((active + 1) % 4, transitionMode)` and disarms C05 auto-return; mode 2 calls `scheduler.trigger()`. `onsetCount`
  (atomic) feeds the UI meter. Switching from Off to an active mode resets the detectors.
- Quantization: the scene change still obeys `transitionMode`, so a hit under "Next Bar" lands on the next bar line.

## 3. Parameters

| ID | Type | Range | Default |
|---|---|---|---|
| `audioTrigger` | Choice | Off, Next scene, Trigger queued | Off |
| `audioTriggerSens` | Float | 0..1 | 0.5 |

## 4. State keys

APVTS parameters only. `onsetCount` is a transient counter.

## 5. Real-time and threading

Detector state is plain floats in the audio thread; the UI reads only the atomic counter. Latency from audio to request is at most one host
block; the scheduler adds its own quantization. No locks, no allocation.

## 6. UI wiring

Input tab: "Audio trigger (F07)" with "On transient" combo, "Sensitivity" slider (attachments to the two parameters) and an onset counter label.
Perform/main scene row shows the resulting queued scene.

## 7. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F07-T01 | Each click detected once; result independent of block size | EXISTS | `tests/core/OnsetDetectorTests.cpp` "detects each click once at 480-sample blocks", "block-size independent" |
| F07-T02 | Silence and steady tone trigger nothing; start-up transient bounded; recovers after silence | EXISTS | OnsetDetectorTests "silence triggers nothing", "steady tone triggers nothing after start", "constant-level signal gives at most the start-up transient", "detector still works after long silence" |
| F07-T03 | Non-finite input is safe | EXISTS | OnsetDetectorTests "non-finite input is safe" |
| F07-T04 | Onsets are counted and "Next scene" advances the scene | EXISTS | `tests/PluginProcessorTests.cpp` "onsets detected", "audio trigger advanced the scene" |
| F07-T05 | "Trigger queued" mode fires a Manual-mode queued switch | DEFERRED | Processor-level test needs the JUCE build, which the core-only authoring machine cannot run; open as TODO B-02. |
| F07-T06 | Sensitivity monotonic: higher sensitivity never detects fewer onsets on the same fixture | EXISTS | `OnsetDetectorTests.cpp (label F07-T06)` |
| F07-T07 | Refractory: two clicks closer than 120 ms yield one onset | EXISTS | `OnsetDetectorTests.cpp (label F07-T07)` |
| F07-T08 | Parameters exist, default Off, restore from project | PARTIAL | `PluginProcessorTests.cpp` "new parameters exist" Parameter existence is asserted; restore from a project relies on the generic APVTS restore. |
| F07-T09 | Realistic material fixtures (drum loop, bass-heavy, dense sustained) with retrigger rate measured | DEFERRED | No rendered drum-loop / bass-heavy / dense-sustained fixtures with a measured retrigger rate. Open as TODO B-07. |
| F07-T10 | C05 auto-return is disarmed by a trigger-requested scene | DEFERRED | Processor-level test needs the JUCE build, which the core-only authoring machine cannot run; open as TODO B-02. |

## 8. Gaps

Unowned, deferred: dedicated sidechain bus (CLAP/VST3 capability flags), per-band detection (would pair with F10), tuned thresholds on real
material, onset-to-sample-accurate request (today up to one block late). Only scene actions exist; there is no trigger that fires gestures
or macros.

## 9. Non-goals

Pitch or beat tracking, ML onset models, a latency-compensated look-ahead detector.
