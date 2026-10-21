# F02 Scene morphing

Status: IMPLEMENTED (effect settings, placement, regions and lane events; W3-MORPH). Beta audit: processor-level checks F02-T06 are open (TODO B-02). Updated 2026-10-20 by the A1 completeness audit.
Replaces draft `docs/expansion/features/F02-SCENE-MORPHING.md` (PR #6). The draft's minimum-cost event correspondence is
replaced by the simpler deterministic rules in section 8.


## Beta audit status (2026-10-20)

- Built: `morph()` blends settings AND control state per section 8 and `docs/specs/W3-MORPH.md`; the processor evaluates it off the per-sample path.
- Section 8 below is now the as-built behavior, not a target. Deviations are recorded in `docs/coordination/DECISIONS.md`.
- Open: a processor test that a morph target equal to the sounding scene is a no-op (F02-T06, B-02); the editor does not preview the morph result.
- Acceptance rows below carry audited statuses; the full join of ID, test, implementation and UI wiring is in [COMPLETENESS-MATRIX.md](COMPLETENESS-MATRIX.md).

## 1. Objective

Blend the SOUNDING scene toward another scene by a host-automatable amount, without editing any stored scene.

## 2. As implemented

- `core::control::morph(a, b, weight)` (`src/core/control/VariationBlend.cpp`), weight clamped 0..1, non-finite -> 0:
  - both sides enabled: `enabled=true`, depth linear, slice log2-linear;
  - only `a` enabled: depth `a.depth * (1-w)`, enabled while depth > 0;
  - only `b` enabled: settings of `b`, depth `b.depth * w`, enabled while depth > 0;
  - neither: `a` unchanged.
  Control state (placement, lane, permissions, activity, phase) is taken from `a` unchanged.
- `GrooveMorphAudioProcessor::effectiveSettings(variation)`: returns the base scene when `morphTargetIndex < 0`,
  `morphWeight <= 0`, or the target equals the scene being rendered; otherwise `morph(base, target, weight)`.
  Evaluated per render segment (`renderSegment`), on the audio thread, 5 operations, no allocation.
- Parameters are read once per block (`process()`): `morphTargetIndex = loadIndex(morphTarget,4) - 1`,
  `morphWeight = clamp(morphAmount,0,1)`. There is no per-sample smoothing; the engine's fixed 3 ms taps
  (`RhythmEngine::crossfadeSeconds`) soften parameter steps (listening-test item in `docs/alpha/ALPHA-FEATURE-STATUS.md`).
- Morph applies on top of the SOUNDING scene (including the evolution candidate, F05) and before placement gating.

## 3. Parameters

| ID | Type | Range | Default | Host-automatable |
|---|---|---|---|---|
| `morphTarget` | Choice | Off, A, B, C, D | Off (0) | yes |
| `morphAmount` | Float | 0..1 | 0 | yes; MIDI macro CC 22; gesture target 0 (F11) |

Declared in `createParameterLayout()` with version hint 1. IDs are append-only and must not be renumbered.

## 4. State keys

Both parameters are APVTS state (`GrooveMorphState` child `PARAM` entries). No scene keys. Missing parameters in an old
project restore to their defaults (`setStateInformation` resets parameters absent from restored state).

## 5. Real-time and threading

No locks, no allocation. Scenes are read from `audioScene.snapshot`, the audio-thread copy obtained by `pullScene()` (SpinLock try-lock;
previous copy kept on contention). Parameter reads are atomics cached in `ParameterCache`.

## 6. UI wiring

Morph tab (`MorphTab`, `src/ui/InspectorPanel.cpp`): "Morph toward" combo (ComboBoxAttachment to `morphTarget`), "Amount" slider
(attachment to `morphAmount`), explanatory note. Perform tab repeats target and amount. Visual window Orbit view draws morph.
The editor does not preview the morph result as a diagram.

## 7. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F02-T01 | Weight 0 keeps `a`; one-sided operations fade out/in | EXISTS | `tests/core/SceneControlTests.cpp` "morph weight 0 keeps a", "morph fades out", "morph fades in" |
| F02-T02 | Two-sided depth linear, slice log-linear midpoint | EXISTS | `CompletenessTests.cpp (label F02-T02)` |
| F02-T03 | Non-finite / out-of-range weight is safe | EXISTS | `CompletenessTests.cpp (label F02-T03)` |
| F02-T04 | Parameters exist, are restored, default Off/0 | PARTIAL | `PluginProcessorTests.cpp` "new parameters exist"; `PluginProcessorTests.cpp` "state restores intensity" Parameter existence and defaults are asserted; a morphTarget/morphAmount restore round trip is not separately asserted (generic APVTS restore is: state restores intensity/tempo/meter). |
| F02-T05 | CC 22 macro drives `morphAmount` | EXISTS | PluginProcessorTests "CC 22 sets morph amount" |
| F02-T06 | Morph target equal to the sounding scene is a no-op | DEFERRED | Processor-level test needs the JUCE build, which the core-only authoring machine cannot run; open as TODO B-02. The pure morph() is covered; the 'target equals sounding scene' guard in effectiveSettings is not. |
| F02-T07 | Morph does not change stored scenes or undo history | EXISTS | `MorphControlTests.cpp` "F02-T08h inputs unchanged"; `PluginProcessorTests.cpp` "stored scene is untouched"; `PluginProcessorTests.cpp` "evolution does not modify the saved scene bank" morph() takes const inputs and the processor evaluates it on the audio-thread snapshot copy; a dedicated processor assertion on the undo stack is not present. |
| F02-T08 | Placement/event morph per section 8 (weight 0 identity, weight 1 equals target control, monotonic coverage, capacity bound, no allocation) | EXISTS | `MorphControlTests.cpp` "F02-T08a weight 0 is a"; `MorphControlTests.cpp` "F02-T08b placement from b"; `MorphControlTests.cpp` "F02-T08g capacity bound" |
| F02-T09 | Block-split independence of a morphed render | EXISTS | `W3ProcessorTests.cpp` "W3-MORPH is deterministic across block sizes"; `W3ProcessorTests.cpp` "W3-MORPH baseline (no morph) is block-size independent" |

## 8. Placement and event morph (implemented by W3; this was the target and is now the behavior)

Today placement window, events, regions, activity limits and phase always come from scene `a`. Target (W3 may refine; any
deviation must be recorded in `docs/coordination/DECISIONS.md`):

1. Discrete placement fields (`window`, `every`, `cadenceOffset`, `originTicks`, `subdivision`, `regions`, `downbeatProtectBeats`,
   `activity`, `phase`): from `a` while `w < 0.5`, from `b` when `w >= 0.5`. Continuous `startPhase`, `spanPhase`: linear.
2. Events: per operation, an event matched in both scenes by `(operation, onsetTicks, durationTicks)` interpolates `depth`, `sliceBeats`
   (log2) and `priority`. An unmatched `a` event scales depth by `(1-w)`; an unmatched `b` event by `w`; events whose depth reaches 0
   are inactive. Same-operation overlap between an `a` and a `b` event is resolved by `w < 0.5 ? a : b` (no overlap in the result).
   Result is capped at `maxLaneEvents` (256 after W3), ties broken by lower index, deterministic.
3. `lane.explicitEvents` is true if either side is explicit; a legacy-uniform side contributes one full-phrase event per enabled operation
   (same as `convertLegacyToEvents`).
4. The merged `SceneControl` is computed off the per-sample path: only when `morphWeight`, `morphTargetIndex` or the scene snapshot
   changes, into a preallocated member of the processor (a 256-event `SceneControl` is several KB; never build it on the audio stack
   per segment).
5. Locks (C03) never apply to a morph (sounding result only); stored scenes stay untouched.

Not owned, remaining: editor preview of the morph result; per-sample smoothing of the amount (non-goal for Beta; engine taps suffice).

## 9. Non-goals

Editing scenes by morphing ("bake" is a separate explicit future command). Morphing across more than two scenes. Morphing the
operation set (a one-sided operation only fades by depth).
