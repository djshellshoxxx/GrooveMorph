# W3-DRY-RETURN: C05 "Dry" automatic-return target

Status: SPEC (2026-10-20, base `c83e8b2`). Implements "Dry (value 5)" of [C05](../control-specs/features/C05-AUTOMATIC-RETURN.md) section "Dry target and DSP chain".

## 1. Objective

After the configured number of bars (or phrases) the plugin returns to the UNPROCESSED signal on the bar line, using the engine's own ramp, without switching scenes, writing parameters, setting bypass or editing a scene.

## 2. Parameters

`autoReturnTarget` choice list becomes "Previous", "A", "B", "C", "D", "Dry" (value 5 appended at the end; index meaning of 0..4 unchanged; default stays Previous).
APVTS stores the plain choice index, so saved projects keep their target. A host that stored NORMALISED automation for this parameter sees values rescale from `i/4` to `i/5`
(a documented effect of appending; this is the one choice list W3 lengthens because the C05 spec fixes Dry = 5).

## 3. Behaviour

- Arming: when a user switch fires with `autoReturnTarget == Dry` and a return length > 0 (not auditioning), `AutoReturn` is armed for Dry at the bar of the switch
  (`target bar = floor(position in bars) + length`, same rule as scene returns).
- Firing: at the sample where the playing position reaches the target bar, the processor sets the DRY OVERLAY. The overlay forces the wet amount handed to the engine to 0;
  the engine's intensity ramp (3 ms) is the ramp. Block and MIDI splits do not move the sample (the segment is split at the boundary, like scene returns).
- The committed scene stays the launch scene; Scheduler, scene bank, `masterIntensity`, `dryWet`, `bypass` and undo are untouched. Host/internal bypass keeps final precedence.
- Clearing the overlay (wet returns with the same 3 ms ramp): any scene request including a request for the already active scene (the processor's `requestScene` wrapper), Cancel,
  Reset/state restore, `prepareToPlay`, internal transport rewind/stop, host seek/loop discontinuity, and a new Dry arming. Transport stop alone does not clear it.
- Overlay and armed state are never saved.
- Status: `getDryReturnState()` returns 0 none, 1 pending (armed for Dry), 2 active. `getAutoReturnTarget()` keeps meaning "scene index of a pending scene return, else -1".
- A return that is itself a Dry fire never arms another return.

## 4. Code

`AutoReturn` (portable): `armDry(positionInBars, bars)`, `isDry()`, `dryBoundaryOffset(positionInBars, samplesPerBar) -> optional<int64>` (samples from `positionInBars` to the target bar
boundary; 0 when reached; nullopt when not armed for Dry). `target()` is nullopt for Dry; `poll()` never requests a scene for Dry. Processor: `dryOverlay` (audio-thread bool) and status atomic; the segment loop
cuts a segment at the boundary exactly like it does for scene switches.

## 5. UI

Minimal: the target combo gets the item "Dry (no effects)" and the status line shows "Dry return pending" / "Dry: effects off until you pick a scene". Everything else is host-visible state.

## 6. Acceptance tests (`tests/core/AutoReturnDryTests.cpp`, `tests/W3ProcessorTests.cpp`)

| ID | Check |
|---|---|
| C05-T06 / W3-DR-T01 | Core: `armDry` at bar 1.3 with 4 bars targets bar 5; offsets for positions 4.5, 4.999, 5.0, 5.2 at 96000 samples/bar are exact; non-finite inputs disarm. |
| W3-DR-T02 | Core: scene arming unchanged (existing AutoReturn tests pass); `isDry()` false for scene arming; `target()` empty for Dry. |
| W3-DR-T03 | Processor: switch A->B with target Dry and 2 bars lands on bar boundary sample (+-0): wet output before the boundary differs from the input (gate/repeat scene), from boundary + ramp length output equals the input bit-exactly. |
| W3-DR-T04 | Processor: parameter values (`masterIntensity`, `dryWet`, `bypass`, `activeVariation`), scene bank and undo are unchanged by the Dry return; active scene stays B. |
| W3-DR-T05 | Processor: a request (even for the active scene), Cancel, seek and prepare each clear the overlay; the effect returns. |
| W3-DR-T06 | Processor: block sizes 1/64/257/4096 give the same boundary sample; a MIDI event at the boundary does not move it. |
| W3-DR-T07 | Processor: host bypass wins; Dry never arms from an audition. |

## 7. Non-goals

Hold / Return-now / countdown UI (C05 UI), per-scene return configs, return quantization other than the bar line, a Dry fade longer than the engine ramp.
