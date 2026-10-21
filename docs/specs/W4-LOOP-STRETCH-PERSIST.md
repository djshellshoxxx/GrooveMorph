# W4 / F09 follow-up: pitch-preserving stretch, loop persistence, capture hardening, waveform

Status: spec written before code (Beta workplan, worker W4). Baseline: alpha F09 (beat-locked varispeed loop, session-only audio).

## 1. Objective

1. Optional pitch-preserving time stretch of the loop to the host tempo (default OFF = today's varispeed, bit-identical).
2. Loop audio is saved inside the plugin/project state and restored on load (old projects unchanged).
3. Capture detects tempo/meter changes while recording; a finished capture is never discarded by a failed publish; the demo loop honours the meter.
4. Waveform overview with beat/bar grid and playhead on the Input tab.

Non-goals: formant/transient-aware stretching beyond WSOLA, real-time stretch, stretch of live input, per-scene stretch, ratios outside 0.5..2.0, loops longer than 20 s, changing the beat-locked reader (`LoopSource::render`).

## 2. Parameter and state keys (append only)

| Item | ID / key | Range | Default | Scope |
|---|---|---|---|---|
| APVTS bool "Loop Stretch" | `loopStretch` (version 1) | off/on | off | scene-independent, host automatable, saved with parameters |
| State child `LoopAudio` | props `v` (int, 1), `blob` (base64 of the binary blob below), `source` (int 0..2) | | absent | project state; absent in old projects |

Old state (no `loopStretch`, no `LoopAudio`): `loopStretch` falls back to its default (off) through the existing "missing parameter" path, the current session loop is left untouched, behaviour is the alpha behaviour.

## 3. Time stretch (`core/dsp/TimeStretcher`)

- WSOLA, frame `N = 2*Hs`, `Hs = round(0.020 * rate)` (>= 64), periodic Hann window (50 % overlap sums to 1), search tolerance `+-Hs/2` around the nominal analysis position, normalized cross-correlation of the overlap region against the natural continuation of the previous frame, coarse search on a decimated mono mix then a full-rate refinement. Stereo: positions are chosen on the channel mean and applied to every channel (keeps the stereo image coherent).
- `ratio = output length / input length`, valid 0.5..2.0 (outside: rejected). Output length is exactly `llround(n * ratio)`. Ratio 1 copies the input.
- Optional circular mode (used for loops): input is read as periodic, `Hs` extra samples are synthesised past the end and cross-faded (raised cosine) into the head so the wrap point is continuous.
- Non-real-time, allocates, cooperative cancel flag. Never called from the audio thread.
- `planStretch(nativeBpm, hostBpm, ratioInUse, maxRatioForCapacity)` is the pure decision function: `ratio = nativeBpm / hostBpm` clamped to 0.5..2.0 and to the 20 s buffer capacity; `limited` reports clamping; `needsNew` when the ratio differs from the one in use by more than 0.2 %.
- `StretchWorker` (std::thread) runs one job at a time; a new submit supersedes the pending one and cancels the running one; results carry a job id.

### RT behaviour
The processor keeps the original loop (`loopStore`, device rate). On the 20 Hz message-thread timer, when `loopStretch` is on and a loop exists, the host tempo must be stable for 200 ms, then a job is submitted. The finished buffer is published with the existing `LoopSource::load` reader-flag handoff (audio thread: no lock, no allocation). Re-stretch happens when the planned ratio changes by > 0.2 %. A loop load/clear/capture, prepareToPlay, or turning the option off bumps a generation counter so stale results are dropped, and the unstretched loop is re-published. Between a tempo change and the new buffer the loop plays varispeed relative to the buffer's tempo (documented artifact). Ratios beyond 0.5..2.0 or beyond the 20 s capacity are clamped and the remainder is varispeed (`limited`).

## 4. Persistence (`core/dsp/LoopBlob`)

Blob v1 (little endian): magic `GMLB`, u16 version, u8 stored channels (1 or 2), u8 flags (bit0 = duplicate the stored channel to stereo), u32 sample rate, u32 samples, f64 loopBeats, f64 originBeats, u32 payload bytes, payload, u32 CRC-32 of all preceding bytes. Audio is quantised to 16-bit PCM (clamped to +-1; lossy relative to float, documented), per channel (stereo: channel 0 and side = R-L) coded with a fixed order 0..3 predictor and Rice codes per 4096-sample block.

Limits: 8 kHz..384 kHz, 16..(20 s * rate) samples, blob <= 10 MiB, loopBeats in (0, 4096], finite origin. The decoder rejects bad magic/version, truncation, trailing bytes, CRC mismatch, sample counts the payload cannot hold, out-of-range fields and residuals that overflow. Plugin state limit rises from 1 MiB to 16 MiB. Over-limit encodes are not saved and a warning is exposed (`getLoopPersistWarning()`). The encoded blob is cached per loop generation (hosts call getStateInformation often).

Restore (`setStateInformation`, message thread): decode, store as `loopStore`, publish through the same path as prepareToPlay (resample to the device rate), restore the source mode only when a loop was restored. Nothing runs on the audio thread. Failure: keep the current loop, set a warning.

## 5. Capture hardening

- `LoopCapture::process` aborts with failure 3 when samples-per-beat drifts more than 0.1 % from the value at the start of recording, and failure 4 when beats-per-bar changes while recording. UI messages for both.
- `pollLoopCapture` always copies the finished capture into the message-thread store before publishing; a failed publish keeps it and retries on the next timer tick (failure 5 is reported after 20 failed attempts; the loop stays stored and keeps being retried by the stretch/republish tick).
- Re-publish after prepareToPlay keeps the capture's origin beat (was reset to 0).
- `DemoSource::render` takes `beatsPerBar` (default 4 = unchanged output): kick every beat, snare on odd beats of the bar (an unpaired last beat of an odd meter has none).
- A loop keeps its length in beats when the meter changes later (documented); the waveform bar grid follows the current meter.

## 6. Waveform (`core/dsp/WaveformPeaks`, `src/ui/WaveformView`)

`computePeaks` -> min/max per column over `min(columns, n)` equal sample ranges (NaN counted as 0), channels combined by extreme. `computeBeatGrid(loopBeats, originBeats, beatsPerBar)` -> beat lines as loop fractions, flagged bar lines (<= 2048 lines). `loopPlayFraction(beat, origin, loopBeats)` -> [0,1). The processor exposes `getLoopPeaks(columns)` (cached per generation) and `getLoopPlayFraction()` (the audio thread publishes the last timeline beat in an atomic). `WaveformView` draws columns, grid and playhead; no interaction.

## 7. Acceptance tests

| ID | Test |
|---|---|
| ST-1 | Output length is `llround(n*ratio)` for ratios 0.5, 0.75, 1, 1.25, 1.5, 2.0, mono and stereo, circular and not |
| ST-2 | Pitch of a 220 Hz sine and a 3-harmonic tone preserved within 10 cents at ratios 0.5, 0.8, 1.5, 2.0 (autocorrelation estimator) |
| ST-3 | No NaN/inf; RMS change within +-1.5 dB on tones |
| ST-4 | Ratio 1 returns the input exactly; invalid ratio/input rejected; ratio outside 0.5..2 rejected |
| ST-5 | Stereo: channels with different content keep their pitch; identical channels stay identical |
| ST-6 | Circular wrap: the step across the loop seam is not much larger than the largest in-loop step |
| ST-7 | Cancel flag aborts; `StretchWorker` returns a correct result, supersedes older jobs, joins cleanly |
| ST-8 | `planStretch`: ratio = native/host, clamp + limited flag, hysteresis |
| PB-1 | Blob round trip: mono, stereo, dual mono, silence, full scale; loopBeats/origin/rate preserved; sample error <= 1/32767 |
| PB-2 | Hostile blobs rejected: empty, bad magic/version, truncation at every length, flipped bytes (CRC), trailing bytes, huge sample count, rate out of range, NaN beats, oversized |
| PB-3 | Encode refuses > 20 s and non-finite metadata; compressed size of a typical loop is smaller than raw 16-bit |
| CP-1 | Capture aborts on tempo change (3) and meter change (4); constant tempo still completes |
| CP-2 | Demo with beatsPerBar 4 is bit-identical to the old output; 3/4 and 6/8 follow the snare rule |
| WF-1 | Peaks: partition covers every sample once, exact min/max on constructed data, NaN safe, columns > n clamps |
| WF-2 | Beat grid positions and bar flags for several origins/meters; play fraction wraps |
| Processor/UI | Not testable in core: recorded as unverified unless a native build is run |
