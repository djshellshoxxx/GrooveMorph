# N04 Print to WAV

Status: specification (2026-10-20), written before code. Owner: worker W7.

## 1. Objective

Capture the PROCESSED output of N bars (1..16) while the transport plays, starting on the next bar line,
into a bounded buffer allocated outside the audio thread. On the message thread write it as a 24-bit
stereo WAV into a per-user Prints folder, keep the last 20 prints listed in the UI, and let the user drag
the file straight into the DAW, reveal it, delete it, or feed it back as the loop source (resampling).

This is separate from F09 input capture (`LoopCapture`, pre-effect signal, 20 s cap). `LoopCapture` is not
changed.

## 2. Non-goals

- No offline/faster-than-real-time render; a print is real time, from the playing transport.
- No MP3/FLAC/other formats, no dither (24-bit integer from float, round to nearest, hard clip at +-1).
- No cloud, no print inside the project state (WAV files are not saved in the project).
- No true "effects-only tail after the source stops": the tail is simply the processed output that follows
  the last bar (see 3).
- Drag-out on a platform where JUCE cannot start a native file drag is a known limitation, not a failure.

## 3. Options and defaults

| Option | Range | Default | Stored |
|---|---|---|---|
| Bars | 1..16 | 4 | UI only (not saved) |
| Wet only | off / on | off (full output) | UI only |
| Include tail | off / on | off | UI only |

No new host parameters, no new scene keys, no serialization change. Old states are unaffected; no legacy
fixture test is required because no persisted format changes.

- Full output: what the plugin outputs (including dry share, band bypass, bypass blend).
- Wet only: the effect signal with the dry share removed. With `a = Intensity * DryWet` the engine output is
  `out = dry + a * (wet - dry)`, so `wet = dry + (out - dry) / a`. Applied per sample when `a >= 0.01`;
  below that the wet signal is not recoverable and the print falls back to the full output (the UI says so).
  With band split or Bypass active the result is an approximation (documented in the help page).
- Tail: appends `min(engine tail, 8 s)` after the last bar, where the engine tail is
  `getTailLengthSeconds()` (history 4 s + crossfade). The tail is the processed output that continues after the
  bar-aligned body.

## 4. Limits (hard caps)

- Body length cap: 90 s of audio at the session rate (16 bars at 4/4 needs >= 42.7 BPM). Longer requests are
  refused at the bar line (`Failure::TooLong`), never truncated.
- Tail cap: 8 s. Total buffer cap: (90 + 8) s * rate, 2 channels, float. At 48 kHz that is 37.6 MB; at 192 kHz
  150 MB. Memory is allocated by `arm()` on the message thread and freed by `release()`; it is NOT allocated at
  idle, and never on the audio thread.
- Prints list: last 20 entries (`kMaxListed`). Older files stay on disk and are not deleted by the app.
- Use as loop source: limited by the loop service (20 s); longer prints are refused with a message.

## 5. Core: `src/core/dsp/OutputCapture.{h,cpp}` (portable, no JUCE)

### 5.1 State machine

`State { Idle, Armed, Recording, Done, Aborted }` (atomic int, seq_cst transitions).

| From | Event | To |
|---|---|---|
| Idle | `arm(bars, beatsPerBar, tailSeconds)` (message thread) | Armed |
| Armed | `cancel()` | Idle |
| Armed | block containing the next bar line while playing | Recording (start sample = round(offset)) |
| Armed | requested body > cap | Aborted (TooLong) |
| Recording | `written == body + tail` | Done |
| Recording (body part) | `playing == false` | Aborted (TransportStopped) |
| Recording (body part) | position discontinuity > 1 ms (seek/loop) | Aborted (Jumped) |
| Recording (body part) | samples-per-beat changes by > 1e-6 relative | Aborted (TempoChanged) |
| Recording (body part) | invalid timing (NaN/inf/<=0) | Aborted (Jumped) |
| Recording | `cancel()` | Idle |
| Done / Aborted | `release()` (message thread, after reading) | Idle, buffers freed |

In the tail part (`written >= body`) the timing checks no longer apply: the tail keeps filling even if the
transport stops or the host seeks, because the tail is "whatever the plugin outputs next".

### 5.2 Exactness

- Start: the first bar line at or after the block start (1e-9 beat tolerance), start offset rounded to the nearest
  sample, so a block that begins exactly on the bar line starts with offset 0.
- Body length = `llround(bars * beatsPerBar * samplesPerBeat)` evaluated at the start; sample exact.
- Block-split invariance: for any partition of the same signal into blocks the recorded buffer is identical
  (test N04-C03).
- Non-finite input samples are stored as 0. A mono input is duplicated to both channels.

### 5.3 Real-time rules

`process()` and `abortBody()` are the only audio-thread calls: no allocation, no locks, no I/O. Status
(`state`, `failure`, `progress`) is published through atomics. A `busy` flag (set before the state is read, cleared
on exit) lets the message thread wait, bounded, before freeing buffers, so `release()` cannot race a block in flight.

### 5.4 Naming helpers: `src/core/dsp/PrintNaming.{h,cpp}`

- `sanitizeComponent(text, maxLen=32)`: keeps ASCII letters/digits and `-`; spaces and `_` become `-`; everything
  else (including path separators, dots, control and non-ASCII bytes) is dropped; runs of `-` collapse; leading and
  trailing `-` are trimmed; empty result -> `"Scene"`; Windows reserved device names (CON, PRN, AUX, NUL, COM1-9,
  LPT1-9, case-insensitive) get a trailing `-` appended.
- `formatBpm(bpm)`: rounded to 0.1, `120` for whole values, `123.5` otherwise; invalid -> `0`.
- `formatTimestamp(Timestamp)` -> `YYYYMMDD-HHMMSS`.
- `buildPrintFileName(scene, bpm, bars, ts)` -> `GrooveMorph_<scene>_<bpm>bpm_<bars>bars_<timestamp>.wav`.
- `withCollisionSuffix(name, n)` inserts `-n` before `.wav` (n >= 2).
- `parsePrintFileName(name)` parses from the right; returns scene, bpm, bars, valid.
- `isPrintFileName(name)`.

## 6. Processor wiring (additive)

- `core::OutputCapture outputCapture;` member; `prepare()` in `prepareToPlay` next to `loopCapture.prepare`.
- `renderSegment`: after the last processing stage of each sub-block, if the state is Armed/Recording call
  `outputCapture.process(...)` with the post-effect channels, the sub-block's global beat and `rhythm.isPlaying`.
  Wet only uses a preallocated scratch buffer plus `dryBuffer` (the dry copy is made when a wet-only print is
  active). No other audio-path change.
- Host discontinuity site: also `outputCapture.abortBody(Jumped)`.
- Message-thread API: `armPrint(bars, wetOnly, includeTail)`, `cancelPrint()`, `getPrintState()`,
  `getPrintFailure()`, `getPrintProgress()`, `takePrintResult(PrintResult&)` (copies Done audio out under
  `loopServiceMutex`, then releases), `importLoopFile(..., trimToSeconds)` (optional trailing parameter,
  default 0 = unchanged behaviour).
- A completed print stays in Done until the Print panel collects it (it polls at about 20 Hz). If the editor is
  closed meanwhile the print is collected when the editor is reopened.

## 7. UI: `src/ui/PrintPanel.{h,cpp}`

New "Print" tab (one extra tab; hook is a few lines in `InspectorPanel.cpp`). Controls: Bars (1..16), Wet only,
Include tail, Print / Cancel button with a status line (waiting for bar line / recording with percent / writing),
a list (newest first, 20) with name and duration, buttons Reveal, Delete, Use as loop, and drag-out from a row
(`juce::DragAndDropContainer::performExternalDragDropOfFiles`, started from `mouseDrag`).

- Folder: `<userDocumentsDirectory>/GrooveMorph Prints`, created on first print. On construction the folder is
  scanned for `GrooveMorph_*.wav` (newest by modification time, max 20).
- File write: 24-bit `juce::WavAudioFormat`, stereo, session sample rate, on the message thread. Name collisions
  get `-2`, `-3`... Failure shows a message and keeps nothing.
- Delete: `File::moveToTrash()`, falling back to `deleteFile()`; entry removed from the list.
- Use as loop source: trims the tail (exact body length for prints made this session; inferred from bars/BPM for
  files found on disk), sets source mode to Captured/imported loop, reports refusal text if over 20 s.
- Errors are shown in the panel; nothing is thrown.

## 8. Acceptance tests

| ID | Requirement | Test |
|---|---|---|
| N04-C01 | Idle by default; `arm` validates bars 1..16, beatsPerBar 1..16, tail 0..8 s; rejects when busy | `OutputCaptureTests` |
| N04-C02 | Armed waits for the next bar line, records exactly `bars*bpb*spb` samples (rounded) and ends Done; mid-bar arm starts at the next bar | `OutputCaptureTests` |
| N04-C03 | Block-split invariance: identical recording for many block partitions incl. 1-sample blocks and non-integer samples per beat | `OutputCaptureTests` |
| N04-C04 | Start exactly on a bar line starts at offset 0 | `OutputCaptureTests` |
| N04-C05 | Abort on transport stop, seek, tempo change, invalid timing; never Done; failure code set; release returns to Idle | `OutputCaptureTests` |
| N04-C06 | Cap: a body above the cap aborts with TooLong at the bar line, never truncates | `OutputCaptureTests` |
| N04-C07 | Tail appended after the body; tail survives a transport stop; body-only abort ignored during tail | `OutputCaptureTests` |
| N04-C08 | Cancel in Armed/Recording returns to Idle; re-arm works; NaN stored as 0; mono duplicates | `OutputCaptureTests` |
| N04-C09 | Progress atomic monotonic 0..1; startBpm/startBeat/loopBeats reported | `OutputCaptureTests` |
| N04-C10 | Wet extraction recovers the wet signal when `a >= 0.01`, passes through otherwise | `OutputCaptureTests` |
| N04-C11 | `release()` frees buffers and a new `arm()` reallocates; audio thread `process` racing `cancel`/`release` does not crash | `OutputCaptureTests` (thread stress) |
| N04-N01 | Sanitiser: separators, dots, unicode, control chars, reserved names, empty, long, runs of `-` | `PrintNamingTests` |
| N04-N02 | `buildPrintFileName` format and `parsePrintFileName` round trip; `withCollisionSuffix`; `isPrintFileName`; bpm formatting | `PrintNamingTests` |
| N04-U01 | Print tab builds, 24-bit WAV is written with correct length, drag-out/reveal/delete/use-as-loop work | CI compile only; behaviour NOT verified (JUCE not buildable on the authoring machine) |
| N04-U02 | Drag into a DAW track | Manual in REAPER; not verified |
| N04-R01 | No allocation/IO/locks on the audio thread in the added path | Code review; `process` uses only preallocated storage and atomics |
