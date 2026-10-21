# F09 Audio import, phrase capture and loop playback

Status: IMPLEMENTED (beat-locked loop, varispeed), spec COMPLETE with W4 targets (stretch, persistence, waveform).
Written 2026-10-20 against `c83e8b2`. Replaces draft `docs/expansion/features/F09-AUDIO-IMPORT-CAPTURE.md` (PR #6).

## 1. Objective

Use a short audio loop as the plugin's input source, either imported from a file or captured from the live input, locked to the
host/internal beat grid so the effects can be auditioned on a repeatable drum pattern.

## 2. As implemented

### 2.1 LoopSource (`src/core/dsp/LoopSource.{h,cpp}`)
- Capacity: `maxSeconds = 20` per loop at the session sample rate; `maxChannels = 2` (mono is duplicated).
- `prepare(rate)` allocates TWO buffers (double buffer) - not real-time safe, called from `prepareToPlay` under `loopServiceMutex`.
- `load(channels, n, numSamples, loopBeats, originBeats)`: message thread, fills the buffer that is NOT published, then publishes with a
  release store. Waits (bounded) until the audio thread's `reading` flag no longer names the buffer it is about to overwrite. Returns false for invalid input.
- `render(channels, n, numSamples, beatAtStart, samplesPerBeat)`: audio thread. Position in the file = `(beat - origin) mod loopBeats / loopBeats * length`
  with wraparound for negative beats, so the loop follows seeks, loop regions and tempo changes. The playback speed is
  `length / (loopBeats * samplesPerBeat)`, i.e. PITCH FOLLOWS TEMPO (no stretching). Returns false and leaves the buffer untouched without a loop or with invalid timing.

### 2.2 LoopCapture
`arm(bars, beatsPerBar)` (message thread) -> state `Armed`; the audio thread starts at the next bar line (`process`), records exactly `bars*beatsPerBar` beats
of the PRE-effect signal, then `Done`; `pollLoopCapture()` (20 Hz timer) copies it into `LoopSource` and the message-thread `loopStore`. Refused when the loop would
exceed 20 s (failure 1); aborted when the transport stops or a host discontinuity occurs mid-recording (failure 2). `cancel()` wins over a finishing block.

### 2.3 Import (`importLoopFile`)
WAV/AIFF/FLAC etc. via `juce::AudioFormatManager::registerBasicFormats()`; rejects missing files, unreadable audio, `< 0.05 s`, `> 20 s`, sample counts above
the session capacity. Takes up to two channels, resamples to the device rate with `juce::LagrangeInterpolator`, then `core::mapLoop` decides the beat length
(`src/core/dsp/LoopImport.cpp`): explicit `nativeBpm` (20..400) wins (`beats = seconds*bpm/60`, may be fractional); else explicit `bars` (1..16);
else automatic: the bar count in {1,2,4,8,16} whose implied tempo is within 60..200 BPM and closest to the current tempo. "Set tempo to loop"
(`matchTempoToLoop`) writes `manualTempo` bracketed with a change gesture.

### 2.4 Source selection and persistence
`setSourceMode(0 live | 1 demo | 2 loop)` is an atomic, NOT a parameter and NOT saved. While the transport plays, modes 1/2 REPLACE the input (the live input
does not leak through); when stopped they output silence. `loopStore` (message-thread copy at `loopStoreRate`) lets the loop survive
`prepareToPlay` with a changed rate (`republishStoredLoop`, Lagrange resample). The loop is NOT saved in the project (DECISIONS).

## 3. Parameters

None. Ranges: bars 1..16 or 0 = auto; native tempo 20..400 BPM or 0; capture bars 1..16 (subject to the 20 s cap).

## 4. State keys (today)

None. (Target in section 8: project child `Loop`.)

## 5. Real-time and threading

Audio thread: `LoopSource::render` and `LoopCapture::process` only; no allocation/locks. Message thread: load/import/capture publication under
`loopServiceMutex` (taken by `prepareToPlay`, never by the audio thread). Double-buffer handshake is covered by LoopSourceTests.

## 6. UI wiring

Input tab (`InputTab`): input source selector (live / demo / loop), bars combo (Auto, 1, 2, 4, 8 bars; Capture treats Auto as 2), loop BPM text box (digits and ".", 7 characters, empty = automatic), "Capture" (label changes to "Waiting for bar line (click to cancel)" /
"Recording..." / "Finishing..."), "Import file..." (file chooser filter wav, aif, aiff, flac, ogg; a successful import switches the source to the loop), "Set tempo to loop", "Clear loop", loop info label (beats, BPM, seconds), status message.

## 7. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F09-T01 | Beat-locked render: position follows beat, wraps, negative positions, origin | EXISTS | `tests/core/LoopSourceTests.cpp` "position follows the beat", "loop wraps", "negative positions wrap", "origin beat plays the loop start" |
| F09-T02 | Double buffer reload, clear/reload races | EXISTS | LoopSourceTests "double-buffer reload", "load right after clear", "repeat reload" |
| F09-T03 | Capture: exact length, aligned to the bar line, refuses > 20 s, aborts on stop, cancel wins | EXISTS | LoopSourceTests "exact length", "aligned to the next bar line", "too-long capture is refused, not truncated", "abort when the transport stops", "cancel wins over a finishing block" |
| F09-T04 | Loop mapping: auto bars, explicit BPM, explicit bars, 3/4, invalid inputs | EXISTS | `tests/core/LoopImportTests.cpp` (all) |
| F09-T05 | Import through the processor: wav loads, auto tempo, plays at original speed at native tempo, bad file/bars/tempo rejected | EXISTS | `tests/PluginProcessorTests.cpp` "wav imports", "loop import succeeds", "at 140 BPM the loop plays at its original speed", "missing file rejected", "bad bar count rejected", "bad loop tempo rejected" |
| F09-T06 | Loop plays locked to the bar grid; stopped source is silent; capture with placement active has no duplicated spans | EXISTS | PluginProcessorTests "loop plays back locked to the bar grid", "a stopped loop source is silent", "captured loop has no duplicated spans when placement splits segments" |
| F09-T07 | Pitch-preserving stretch (target 8.1): at tempo/native = 0.5, 0.75, 1.25, 2.0 the dominant frequency of a 1 kHz sine loop stays within 10 cents; output length and beat lock unchanged | MISSING, owned by W4 | `tests/core/LoopStretchTests.cpp` |
| F09-T08 | Stretch is deterministic and block-split independent; re-stretch publishes through the existing double buffer without audio glitches (no discontinuity above the loop's own content) | MISSING, owned by W4 | W4 |
| F09-T09 | Loop persistence round trip in project state (8.2): save, restore into a fresh processor, render equals the original within the codec tolerance (lossless FLAC: bit-exact) | MISSING, owned by W4 | `tests/PluginProcessorTests.cpp` |
| F09-T10 | Oversized/corrupt `Loop` state is rejected and the previous loop kept; legacy project without `Loop` loads | MISSING, owned by W4 | W4 (legacy-fixture test required by the workplan rules) |
| F09-T11 | Waveform data (8.3): min/max peaks deterministic for a known sine, bounded size, safe for NaN/empty | MISSING, owned by W4 | `tests/core/LoopWaveformTests.cpp` |
| F09-T12 | Sample-rate change keeps and resamples the stored loop | MISSING (code `republishStoredLoop` exists) | processor test |

## 8. Gaps and target behavior (owner W4)

### 8.1 Pitch-preserving stretch
Mode `Stretch: Off (varispeed, today) | On`. When On, the loop is rendered OFFLINE (message thread or a worker, never the audio thread) into the
standby buffer with length `loopBeats * samplesPerBeat(currentTempo)`, using a time-domain method (WSOLA or equivalent; algorithm choice is W4's, with
the pitch test F09-T07 as the gate). Because `LoopSource` maps by beat fraction, a buffer stretched to the current tempo plays at ratio 1.0 with no
change to `render`. Rules: supported ratio `tempo/nativeBpm` in 0.5..2.0; outside it the loop plays varispeed and the UI says so. Re-stretch is
debounced (at least 250 ms after the last tempo change) and published with the existing `load()` handshake; while stale, playback continues
varispeed on the old buffer (accepted pitch drift, shown in the UI). Tempo automation re-stretches only when the tempo has moved more than 0.5%
from the tempo the buffer was rendered for. The original (unstretched) audio remains in `loopStore`; stretching never degrades it.

### 8.2 Persistence in the project
`getStateInformation` writes a child `Loop` only when a loop exists: properties `version`=1, `rate`, `channels`, `beats`, `nativeBpm`,
`stretch`, `name` (display only), `format`="flac", `data` (base64 of lossless FLAC of the ORIGINAL unstretched audio, 24-bit). The current envelope limit in
`setStateInformation` is 1 MiB (`maximumStateBytes`); a 20 s stereo loop needs a larger cap. W4 must raise the limit to a documented value (recommended 16 MiB,
only honored when a valid `Loop` child is present), decode on the message thread, validate (`rate` 8000..384000, `channels` 1..2, `beats` > 0 and <= 4096,
decoded length <= 20 s at that rate), and on any failure keep the previous loop and report through `getLastSceneLoadReport`-style status. Source mode is
restored to "loop" only if the loop decoded. Absent child -> no loop (legacy projects). Never write the loop into the scene text.

### 8.3 Waveform data
On load/import/capture the message thread computes a downsampled peak table (for example 1024 columns of `{min,max}` of the channel mix) stored in a
`std::shared_ptr<const LoopWaveform>` swapped atomically; the UI reads it without locks. The playhead is derived from the existing beat status
(`getBeatInBar`, loop beats) on the UI timer. Waveform drawing is a new UI file; no audio-thread work.

### 8.4 Deferred, unowned
Warp markers, transient slicing, stems, sample-accurate import drag/drop, multiple loops, loops longer than 20 s, video.

## 9. Non-goals

A sampler, tempo detection beyond `mapLoop`, writing audio back into the host (see [N04](N04-PRINT-TO-WAV.md) for printing), and
cloud storage of loops.
