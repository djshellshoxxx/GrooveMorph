# W3-C04-SMOOTHING: per-scene transition smoothing time

Status: SPEC (written before code, 2026-10-20, base `c83e8b2`). Implements the "Transition smoothing" control of
[C04](../control-specs/features/C04-TRANSITION-PHASE.md) with the deviations listed in section 9. Related: A06 section 6, W3-PHASE-CAPTURE.

## 1. Objective

Let each scene choose how long the read-position cross-fade lasts when its pattern jumps (scene switch, restart, phase shift, slice change),
instead of the engine's fixed 3 ms. Default behaviour must be bit-identical to today.

## 2. Parameters, ranges, defaults

Scene data (not an APVTS parameter; same decision as the rest of `PhaseConfig`, see DECISIONS "C04 phase is scene content"):

| Field | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `PhaseConfig::smoothingMs` | float | 0..250 | 0 | 0 = engine default (3 ms, today's behaviour). 0 < v <= 250 = cross-fade length in ms. Values below 3 ms are allowed (shorter than default). |

`sanitize(PhaseConfig&)` clamps to 0..250, non-finite -> 0. UI step would be 1 ms (UI is out of scope for W3; the field is reachable through the scene API and file).

## 3. Engine behaviour (`RhythmEngine`)

`RhythmTiming::crossfadeSeconds` (double, default -1; < 0 or non-finite = engine default `RhythmEngine::crossfadeSeconds`). It is read per `process()` call.

- Only READ-POSITION cross-fades use it: when a history voice's read position jumps (existing detector, `|r - predicted| > 0.5`), the new retiring tap gets
  `len = round(clamp(s, 0, 0.25) * sampleRate)` samples. A tap stores its own length, so a later change of the setting never alters a running fade.
- `len == 0` (only reachable with a request below half a sample): no tap, a hard cut.
- Long fades are limited by the time since the voice's previous jump: when `len` exceeds the default fade, `len = min(len, max(defaultFade, samplesSinceLastJump))`.
  Reason: with a 250 ms fade and a 1/16 beat Repeat cycle the two-tap cap would drop the oldest tap (a click). Shorter-than-default requests are never extended.
- Not affected (the C04 text asks for them; recorded as non-goal): operation enable/depth ramps, intensity ramp, gate ramp, bypass ramp. These keep 3 ms / 2 ms.
- Weight arithmetic keeps the exact legacy float expression (`remaining * (1/(len+1))`), so a request equal to the default produces identical bits.
- The two-tap cap, the ring bounds and `readRing` clamping are unchanged; a tap that runs past the oldest valid sample reads the clamped (oldest) sample.

## 4. Processor wiring

The processor keeps an audio-thread `transitionSmoothingMs`. It is set from the captured launch `PhaseConfig` in `applyScenePhase` (switch, Restart, state reset)
and sampled into `rhythm.crossfadeSeconds` per render segment (`ms * 0.001`, or -1 when 0). A scene's smoothing therefore applies from its launch until the next
launch/restart/seek (same capture rule as offset and policy: editing the value of the SOUNDING scene takes effect at its next launch or Restart).

## 5. State keys

`variation.<i>.phase.smooth` (float ms). Written only when != 0 so legacy scenes serialize byte-identically; missing = 0; out of range is clamped with a recovery note;
format stays `groovemorph-scenes v1`.

## 6. Real-time

No allocation. Per voice two extra scalars; per tap one extra float. No change in the per-sample arithmetic when the setting is default.

## 7. UI

None in W3 (UI files are hotspots). A help sentence is added to `help/CONTROLS.md`. The engine and scene API are ready for a slider.

## 8. Acceptance tests (`tests/core/TransitionSmoothingTests.cpp`, `tests/core/EngineGoldenTests.cpp` for the new option)

| ID | Check |
|---|---|
| W3-C04S-T01 | Default (-1), explicit 3 ms and an explicit zero-config reproduce the legacy golden hashes bit-identically (all EngineGolden cases). |
| W3-C04S-T02 | Fade length: after a forced jump a 25 ms request keeps the cross-fade running for 25 ms of samples at 44.1/48/96/192 kHz; the default gives 3 ms. |
| W3-C04S-T03 | Longer smoothing lowers the peak sample step at a scene switch (50 ms < 3 ms) on a sine fixture; output finite. |
| W3-C04S-T04 | Block-split invariance for a 25 ms request (blocks 64/257/1024 give identical output). |
| W3-C04S-T05 | Rapid slice cycles with a 250 ms request never exceed two taps and the step stays within 1.5x of the 3 ms case. |
| W3-C04S-T06 | `PhaseConfig` sanitize (negative, NaN, 1e9); scene file round trip; absent key = 0; legacy scene text unchanged byte for byte. |
| W3-C04S-T07 | Processor: scene B with smoothing 40 ms switched to from A gives a smaller output step at the switch than B with 0 (JUCE test, `tests/W3ProcessorTests.cpp`). |

## 9. Deviations from the C04 text

Range 0..250 ms with 0 = engine default (spec: 3..500 ms, default 3). Stored as scene data, not an APVTS parameter. Operation/gate ramps are not retimed. Per-tap length is
limited by the time since the previous jump (not in the spec). Reason: the task contract fixes 0..250 and bit-identical legacy; the rest follows the existing scene-content decision.
