# W3-LR-CROSSOVER: Linkwitz-Riley band split (F03 / F10)

Status: SPEC (2026-10-20, base `c83e8b2`). Implements the W3 target of F03 section 7 and F10 section 7 (parameter named `bandTopology` as W1 recommended).

## 1. Objective

Replace "a gentle first-order split" with a real Linkwitz-Riley crossover as an option: LR2 (12 dB/oct per side) and LR4 (24 dB/oct per side). Low and high are -6 dB at the crossover and
their (polarity-corrected) sum has flat magnitude. The existing behaviour stays available and is the default.

## 2. Parameters (APPENDED to the layout, version hint 1)

| ID | Type | Choices | Default |
|---|---|---|---|
| `bandTopology` | Choice | "Classic", "Linkwitz-Riley 12 dB/oct", "Linkwitz-Riley 24 dB/oct" | Classic (0) |

Existing `bandMode`, `bandCrossover`, `bandSlope` are untouched (no value repurposed, lists not lengthened). Legacy mapping: `Classic` keeps the old meaning of `bandSlope`
(0 = 6 dB/oct one-pole, 1 = 12 dB/oct complement). `Linkwitz-Riley *` ignores `bandSlope`. Old projects lack `bandTopology` and restore to Classic, i.e. bit-identical to alpha.

## 3. DSP (`src/core/dsp/Crossover.{h,cpp}`, portable, no allocation after `prepare`)

- `LrCrossover`: per channel, zero latency. 2nd-order Butterworth TPT state-variable filters (k = sqrt 2, Zavalishin form, same as the existing 12 dB path):
  - LR4: `low = LP(LP(x))`, `high = HP(HP(x))` (stage 1 shared, stage 2 split into an LP and an HP instance: three SVFs per channel). `low + high` is a 2nd-order all-pass.
  - LR2: two cascaded 1-pole sections: `low = LP1(LP1(x))`, `high = -HP1(HP1(x))` (polarity inverted so `low + high` is the 1st-order all-pass `(1-s)/(1+s)`).
    The effects therefore see the inverted high band in LR2 "Protect lows"; every operation is odd-symmetric, so this is inaudible.
- Frequency: target in Hz clamped to 20..1000 and to 0.45 * fs. The working frequency approaches the target with a one-pole smoother (time constant 5 ms); coefficients are recomputed every
  16 samples while it moves (0.01 % tolerance, then snapped). First use / `prepare` / topology change starts at the target with no glide. TPT filters stay stable under per-chunk coefficient changes.
- Non-finite input samples are replaced by 0 before filtering; non-finite filter state is reset to 0.
- `BandSplit` (same file) owns the buffers (`original`, `bypass`, `part`, sized in `prepare`) and offers:
  - `split(channels, n)`: leaves the EFFECTED band in `channels` (Protect lows = high, Lows only = low) and stores the original and the bypassed band.
  - `merge(channels, n)` after the engine: `x == part` for every channel at a sample means the effects changed nothing there.
    A shared blend `g` (0..1) decides the output: `g == 0` outputs `original` exactly; otherwise `original + g * (x + bypass - original)`, and `g == 1` outputs `x + bypass`.
    `g` rises to 1 over 3 ms from the first changed sample, holds 10 ms after the last changed sample, then falls to 0 over 10 ms.
  - Reason for the blend: an LR sum is an all-pass of the input, not the input. Jumping between "exact dry" and "low + high" would click at every effect onset.
    The blend makes the exactness shortcut a crossfade. Consequence (documented, not a defect): while `g` is between 0 and 1 after an effect ends, the output is a 10 ms mix of dry and the all-pass sum.
- Mode / topology / slope change resets the filter states and the blend (as the existing code does for its states).

## 4. Processor wiring

`bandTopology` cached like the other band parameters; when topology > 0 and `bandMode != 0`, `renderSegment` calls `bandSplit.split(...)` instead of the Classic loops and `merge(...)` after the engine.
Classic code is left byte-for-byte as is (including its dry-buffer exactness shortcut). The `dryBuffer` copy is kept for the host-bypass ramp.

## 5. State

APVTS parameter only. No scene keys.

## 6. UI

Out of W3 scope except a single combo next to the slope combo (small edit, `bandTopology` ComboAttachment) if the file stays mergeable; otherwise the parameter is available to the host. Tooltip: "Classic keeps the
old gentle split. Linkwitz-Riley is steeper and sums flat."

## 7. Acceptance tests (`tests/core/CrossoverTests.cpp`, `tests/W3ProcessorTests.cpp`)

| ID | Check |
|---|---|
| F10-T03 / W3-LR-T01 | Impulse response of LR2 and LR4, each side -6.02 dB (+-0.1) at fc, `|low+high|` flat within 0.1 dB from 20 Hz to min(20 kHz, 0.45 fs) at 44.1/48/96/192 kHz, for fc 40, 120, 400 Hz. |
| W3-LR-T02 | Slopes: LR4 low is down >= 22 dB one octave above fc... measured against the analytic value within 1 dB; LR2 >= 10 dB. |
| F10-T04 / W3-LR-T03 | Stereo: two channels fed different signals are filtered independently; identical input gives bit-identical channels. |
| F10-T05 / W3-LR-T04 | Sample-rate independence: gain at fc and at 4 fc agrees across rates within 0.2 dB. |
| F10-T06 / F03-T09 / W3-LR-T05 | Sweeping fc 40 -> 400 Hz over 50 ms and switching mode/topology mid-stream keeps the maximum sample step within 1.5x the step of the unswept sine (documented bound), no NaN. |
| F03-T08 / W3-LR-T06 | Merge with an engine stub that changes nothing outputs the input bit-exactly for both topologies and both modes; random input, 1000 blocks of random size <= maxBlock. |
| W3-LR-T07 | Merge with a stub that changes every sample by 1 ulp for long enough (g = 1) produces `x + bypass`; the system response to an impulse is flat within 0.1 dB. |
| W3-LR-T08 | Effect onset (stub starts changing at sample N): output step at N is below 2x the steady input step (no click from the exactness shortcut). |
| W3-LR-T09 | NaN/Inf input: finite output, later blocks unaffected (filter states recover). |
| W3-LR-T10 | Processor: `bandTopology` exists, default 0, restores from a legacy state (absent) to 0 and renders bit-identically to the previous Classic render; LR modes give finite output with a closed gate that keeps bass (Protect lows) / gates bass (Lows only). |

## 8. Non-goals

Linear-phase / zero-phase splitting (latency). More than two bands. Smoothing of the Classic path's coefficient (it keeps the per-block closed form so legacy output is bit-identical).
