# GrooveMorph specification index

Updated 2026-10-20 (Beta workplan, worker W1). Baseline: main `c83e8b2`. Every planned feature and TODO item below has an implementation-grade spec:
objective, exact parameter IDs and ranges, state keys, real-time and threading behavior, UI wiring, acceptance tests with IDs (each marked EXISTS or MISSING with its location), gaps with owners, non-goals.
The F01-F12 specs describe the feature AS IMPLEMENTED in `src/` plus the remaining gaps; they replace the INCOMPLETE drafts in unmerged PR #6 (`spec/groovemorph-expansion-v1`, `docs/expansion/`), which are not in main.

How to read a spec: the acceptance table is the contract. A feature is only done when every ID has a passing test or a recorded reason. "Owned by Wn" names the Beta worker that closes the gap (workplan: `docs/coordination/BETA-WORKPLAN.md` on the integration branch).

## Feature specs (as implemented plus gaps)

| ID | Spec | Status in code | Main gaps (owner) |
|---|---|---|---|
| F01 | [Controlled generation](F01-CONTROLLED-GENERATION.md) | Implemented subset | golden fixture, capacity 256 (W3), Euclid/chance (W6) |
| F02 | [Scene morphing](F02-SCENE-MORPHING.md) | Effect settings only | placement and event morph (W3) |
| F03 | [Groove protection](F03-GROOVE-PROTECTION.md) | Implemented subset | Linkwitz-Riley (W3) |
| F04 | [Performance mode](F04-PERFORMANCE-MODE.md) | Implemented subset | keyboard control, thread safety (W10) |
| F05 | [Phrase evolution](F05-PHRASE-EVOLUTION.md) | One scene | multi-scene, preview (unowned) |
| F06 | [Polyrhythmic lanes](F06-POLYRHYTHMIC-LANES.md) | Per-lane loop ring | loop-aware Reduce (W3), ring drawing (unowned) |
| F07 | [Audio triggers](F07-AUDIO-TRIGGERS.md) | Scene actions only | tuning on real material (unowned) |
| F08 | [Generation history](F08-VARIATION-HISTORY.md) | Local file + scene undo | codec extraction and tests (unowned) |
| F09 | [Audio import and capture](F09-AUDIO-IMPORT-CAPTURE.md) | Varispeed loop | stretch, persistence, waveform (W4) |
| F10 | [Multiband](F10-MULTIBAND.md) | Two bands | LR crossover (W3); N-band deferred |
| F11 | [Gesture recorder](F11-GESTURE-RECORDER.md) | One lane | write throttling, DECISIONS fix (W10) |
| F12 | [MIDI, CLAP and platforms](F12-MIDI-CLAP-PLATFORMS.md) | MIDI + VST3/Standalone | CLAP (W9), conflict policy and IDs (W10) |

## TODO-item specs (no other owner)

| ID | Spec | Implementation owner |
|---|---|---|
| A04 | [MIDI duplicate-assignment policy](A04-MIDI-CONFLICT-POLICY.md) (recommends auto-swap with undoable notice) | W10 |
| A06 | [Automation IDs and whole-scene fade policy](A06-AUTOMATION-IDS-AND-FADE.md) | W10 (IDs), W3 (smoothing hook) |
| A07 | [Accessibility, keyboard, scaling, screen reader](A07-ACCESSIBILITY-INPUT.md) | W10 |
| R01 | [Release licensing and third-party notices](R01-LICENSING-NOTICES.md) (decision record; license not chosen) | W9 with the product owner |

## Beta feature specs (written by their implementing workers)

These files are created by the workers named below on their own branches and arrive at integration. Links resolve after merge; until then they are listed as pending in the link check.

| ID | Spec | Worker |
|---|---|---|
| N01 | [Effects: Filter Sweep and Bit Crush](N01-EFFECTS-SWEEP-CRUSH.md) | W2 |
| N02 | [Presets](N02-PRESETS.md) | W5 |
| N03 | [Euclidean patterns and per-event chance](N03-EUCLID-CHANCE.md) | W6 |
| N04 | [Print to WAV](N04-PRINT-TO-WAV.md) | W7 |
| N05 | [Easy mode](N05-EASY-MODE.md) | W8 |
| N06 | [CLAP format and release packaging](N06-CLAP-AND-RELEASE.md) | W9 |

W3 (DSP completeness: smoothing, Linkwitz-Riley, loop-aware Reduce, Dry return, NaN safety, capacity 256 and 8 regions, morph of placement and events) and W4 (loop) write their targets into
the F-specs above (F02, F03, F06, F09, F10, C04, C05) and may add `docs/specs/W3-*.md` / `W4-*.md` notes; W10 (host-automation IDs, MIDI conflict policy, keyboard event creation, accessibility, timer-thread safety, conflict badge) implements A04, A06, A07.

## Control specs and deviations

| Spec | Document |
|---|---|
| C01 Phrase placement | [C01](../control-specs/features/C01-PHRASE-PLACEMENT.md) |
| C02 Variation distance | [C02](../control-specs/features/C02-VARIATION-DISTANCE.md) |
| C03 Change permissions | [C03](../control-specs/features/C03-CHANGE-PERMISSIONS.md) |
| C04 Transition phase | [C04](../control-specs/features/C04-TRANSITION-PHASE.md) |
| C05 Automatic return | [C05](../control-specs/features/C05-AUTOMATIC-RETURN.md) |
| C06 Effect activity limits | [C06](../control-specs/features/C06-EFFECT-ACTIVITY-LIMITS.md) |
| Package | [README](../control-specs/README.md), [shared contracts](../control-specs/SHARED-CONTRACTS.md), [integration plan](../control-specs/INTEGRATION-PLAN.md), [parameters](../control-specs/PARAMETERS.json) |
| Where the code differs | [C01-C06 deviations](C01-C06-DEVIATIONS.md) |

## Other specifications

| Document | Purpose |
|---|---|
| [Visual feedback spec](../visual-feedback/SPEC.md), [plan](../visual-feedback/PLAN.md), [integration plan](../visual-feedback/INTEGRATION-PLAN.md) | Visual window (Warp Weave, Ledger, Rings, Stations, Orbit) |
| [Developer specification](../SPECIFICATION.md) | Original requirements and release contract |
| [Traceability](../TRACEABILITY.md), [Alpha feature status](../alpha/ALPHA-FEATURE-STATUS.md), [Decisions](../coordination/DECISIONS.md) | Requirement coverage, status, design decisions |

## Conventions that bind every spec

1. Host parameter IDs are append-only (`ParameterID` version hint 1). Never reorder, rename, reuse or lengthen an existing choice list (hosts store normalised values). Existing IDs:
   `masterIntensity`, `manualTempo`, `manualMeterNumerator`, `manualMeterDenominator`, `phraseBars`, `dryWet`, `activeVariation`, `transitionMode`, `bypass`, `timingMode`, `morphTarget`, `morphAmount`,
   `autoReturnBars`, `autoReturnTarget`, `autoReturnUnit`, `bandMode`, `bandCrossover`, `bandSlope`, `audioTrigger`, `audioTriggerSens` (in layout order, `createParameterLayout()`).
   The complete current table (generated, 44 rows including the per-operation trims, Chaos, Loop Stretch and Band Topology): [PARAMETER-TABLE.md](PARAMETER-TABLE.md); regenerate with `python3 tools/gen_parameter_table.py`.
2. Scene format stays `groovemorph-scenes v1`; new scene keys are optional and written only when non-default; old files load unchanged; add a legacy-fixture test when touching serialization.
3. Project state root `GrooveMorphState` has children `SceneBank`, `Gesture`, `Evolution`, `MidiLinks`; new children are optional (W4 adds `Loop`, W5/W8 may add their own). Load limit is 1 MiB unless a spec raises it explicitly.
4. The audio thread allocates nothing, takes no locks, does no I/O and never calls the host; message-thread services use `modelMutex`, `gestureMutex`, `loopServiceMutex`, `midiLinkMutex`.
5. Test IDs are `<spec>-T<nn>`. Existing tests keep their names; the spec maps them. New tests should carry the ID in their `expect` label.
6. Status words follow `docs/alpha/ALPHA-FEATURE-STATUS.md`. A spec never claims a measurement that was not performed.
