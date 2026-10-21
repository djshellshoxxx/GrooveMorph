# N01 Filter Sweep and Bit Crush (two new rhythmic operations)

Status: implemented (core tested on Linux; UI written against JUCE 8.0.7, not built locally).

## Objective
Add two operations after TapeStop in `OperationType`, so the chain order (= enum order) becomes
Repeat, ReverseSlice, Gate, HalfTime, TapeStop, **FilterSweep**, **BitCrush**. `operationCount` = 7.
Both are tonal effects: they read no history and move no audio in time (no ring, no extra latency).

## Engine rules (unchanged, both ops obey them)
- Stage output is `x + amount * (opOutput - x)`; `amount = enabled ? depth : 0`, slewed over `crossfadeSeconds` (3 ms).
- `intensity == 0` or amount `0` leaves the signal bit-exact.
- Causal, zero latency, bounded work per sample, no allocation in `process()`.
- Invalid timing or stopped transport ramps the amount to 0 (state keeps tracking the input meanwhile).
- `resetHistory()` / `reset()` clear the filter and hold state (transport jump).

## FilterSweep
- Filter: TPT state-variable low-pass (Zavalishin), fixed Q = 2.5 (k = 0.4), double precision state.
- Slice (`sliceBeats`, 1/16..4, default 1/4) is one sweep. Position `p` in [0,1) is measured on the operation's
  slice grid (honours the per-event phase offset like every other stage).
- Cutoff is exponential in `p`: `fc = fHigh * (fLow/fHigh)^q`, with `fHigh = min(20000, 0.45 sr)`, `fLow = min(200, 0.5 fHigh)`.
  Even-numbered slices close (`q = p`, high to low), odd-numbered slices open (`q = 1 - p`), so the cutoff is continuous at every slice edge.
- Depth is the stage amount (dry/wet of the stage). No other parameter.
- Stability: `g = tan(pi fc / sr)` with `fc <= 0.45 sr`; coefficients finite and positive at 8 kHz..384 kHz.
- Denormal and NaN safety: state values below 1e-25 flush to 0; a non-finite result resets the state and passes the input.
- While the stage is inactive its state tracks the input (`ic2 = x`, `ic1 = 0`) so activation does not click.

## BitCrush
- Envelope `e = p` over the slice (0 at slice start, rising linearly, resets at the next slice).
- Bit depth `bits(e) = 16 - 12 e` (16 down to just above 4), fractional allowed; quantiser `round(x L) / L`, `L = 2^(bits-1)`.
- Sample-rate reducer: target rate `fT(e) = 12000 * 2^(-4 e)` Hz (12 kHz down to 750 Hz); hold length `H = max(1, sr / fT)`
  samples (fractional, accumulated), so the sound is the same at any host rate. `H = 1` means no reduction.
- At each capture instant the input is quantised with the bit depth of that moment and the result is held until the next capture.
- Deterministic (no dither, no random); state is the held sample per channel and one shared hold counter.

## State and serialization
- Scene keys (format stays v1): `variation.N.op.filtersweep.{enabled,depth,slice}` and `variation.N.op.bitcrush.{enabled,depth,slice}`.
  They are written only when an effect differs from its disabled default, so scenes that never use the new effects
  and have default scene control serialize byte-identically to earlier builds (a scene with non-default control also lists
  `perm.op.5/6`, `activity.prio.5/6` and `lane.loop.5/6`). Old files load with both new effects disabled.
- Control keys indexed by operation (`perm.op.N`, `activity.prio.N`, `lane.loop.N`, `event.K.op`) accept 0..6.
  `activity.max` accepts 1..7; the default is the lane count (7). An old file that stores `activity.max=5` keeps 5.
- `laneCount` = 7. Default priorities are 50 for every lane.
- No APVTS parameter is added (operations are scene state).

## UI
- Inspector: two more operation rows (enable, depth, slice), lanes, lock rows, priority sliders.
- Visual window: two new effects (glyphs, palette entries, legend, stage names); trace amounts widen to 7.
- MIDI links `s:<effect>:...` accept effect 0..6.

## Acceptance tests
| ID | Test | File |
|---|---|---|
| N01-T01 | Sweep frequency response follows the cutoff trajectory at 8, 44.1, 48, 96, 192, 384 kHz | EffectsSweepCrushTests |
| N01-T02 | Sweep is stable (bounded output for full-scale noise) at every rate and slice length | same |
| N01-T03 | Crush quantization levels match `bits(e)`; hold length matches `fT(e)` | same |
| N01-T04 | Bit-exact bypass: intensity 0, depth 0 or disabled gives the input unchanged | same |
| N01-T05 | Block-split invariance (1, 7, 64, 480, 4096) for both ops | same |
| N01-T06 | NaN, Inf and denormal inputs never produce a non-finite output or poison later blocks | same |
| N01-T07 | Reset on seek: output after resetHistory equals a fresh engine's | same |
| N01-T08 | No memory growth: `historyCapacitySamples()` unchanged, no history ring used | same |
| N01-T09 | Serialization round trip; legacy fixture (five-operation v1 file) loads with new ops disabled and re-serializes without new keys | SceneSerializerTests |
| N01-T10 | Planner / candidate builder mutate and report the new operations | VariationPlannerTests |
| N01-T11 | Palette: effect colours keep contrast and CVD separation | PaletteTests |
| N01-T12 | Trace frame carries amounts for all seven operations | EffectsSweepCrushTests |

## Non-goals
Per-effect resonance/shape controls, filter type choice, dithering, modulated crush, MIDI note map changes
(the note map is per-scene, not per-operation), history rings for the new stages.
