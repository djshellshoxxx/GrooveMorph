# W3-PHASE-CAPTURE: C04 phase configuration is captured at REQUEST time

Status: SPEC (2026-10-20, base `c83e8b2`). Closes the KNOWN-ISSUES line "C04 reads the phase configuration at switch time, not request time".
Implements the C04 rule "Snapshot contains config at REQUEST time; editing knobs later affects future launches, not a queued request".

## 1. Behaviour

When a scene request is accepted (any source: UI, host Variation parameter, MIDI note/program change, audio trigger, automatic return), the audio thread stores
`{variation, PhaseConfig}` of the destination scene's STORED settings at that moment (not the morph/evolution-blended copy). When the Scheduler fires that switch,
`applyScenePhase` uses the stored copy. If nothing matching was stored, the destination's current stored config is used.

- A newer request replaces the stored copy. A request for the active scene (the Scheduler cancels) clears it. Cancel, scene reset, state restore and prepare clear it.
- "Restart scene now" and the seek/reset path read the current config (they are immediate).
- The captured `smoothingMs` (W3-C04-SMOOTHING) travels with it.
- Only phase policy, offset and smoothing are frozen (as C04 specifies). Effect settings and placement of the queued scene stay live.

## 2. Code

`core/control/LaunchPhase.h` (header only, noexcept, fixed size): `capture(variation, config)`, `clear()`, `take(variation, fallback) -> PhaseConfig`
(returns the stored, sanitized config when the variation matches, else `fallback`; always clears). Processor: one private `requestScene(variation, mode)` wrapper used by every
`scheduler.request` call site; it calls the Scheduler, then captures or clears according to `scheduler.queuedVariation()`. The same wrapper clears the Dry-return overlay
(W3-DRY-RETURN).

## 3. Tests (`tests/core/LaunchPhaseTests.cpp`, `tests/W3ProcessorTests.cpp`)

| ID | Check |
|---|---|
| W3-PC-T01 | Capture B, change the fallback, `take(B)` returns the captured config and clears; a second `take` returns the fallback. |
| W3-PC-T02 | `take` for a different variation returns the fallback; the entry is cleared (single-slot rule). |
| W3-PC-T03 | The captured config is sanitized (NaN offset, out-of-range smoothing). |
| W3-PC-T04 | Processor: queue B for Next Bar with B = Restart; edit B to Preserve before the bar; at the bar B still restarts (pattern phase 0). Reverse order keeps Preserve. |
| W3-PC-T05 | Processor: re-requesting B after the edit captures the new config. |

## 4. Non-goals

Freezing effect settings or placement of the queued scene. Showing the frozen policy in the UI.
