# A06 Stable per-operation automation IDs and whole-scene fade policy

Status: SPEC COMPLETE, implementation owned by W10 (IDs) and W3 (smoothing hooks). Written 2026-10-20 against `c83e8b2`.
Source item: `docs/TODO.md` A06 ("define stable operation automation IDs, full-scene duplicate and fade policy"). Related: [F12](F12-MIDI-CLAP-PLATFORMS.md), [F02](F02-SCENE-MORPHING.md), C04.

## 1. Objective

(a) Let a DAW automate each effect (Repeat, Reverse Slice, Gate, Half Time, Tape Stop) without editing scenes, with IDs that never change.
(b) State exactly what is heard when a whole scene is replaced while playing.

## 2. Current behavior (facts)

- Per-operation `enabled`, `depth`, `sliceBeats` live in the scene bank (`OperationSettings`), NOT in host parameters (`docs/coordination/DECISIONS.md`). Hosts cannot automate them.
  The only live path is MIDI links with `s:<effect>:<field>` targets, applied by `setOperationSettingsLive` (amends the undo entry, honors locks, refused during preview).
- Copy FX: `SceneBank::copyVariationSettings` (settings only, keeps name/MIDI/active/seed), processor `copyVariationSettings` (one undo step, refused during audition).
- Whole scene: `duplicateVariation` (settings, control state and name; one undo step), `resetVariation`; both keep the destination's locked fields (`enforcePermissions`).
- Fades: the engine ramps `enabled/depth` over `RhythmEngine::crossfadeSeconds = 0.003` (3 ms), gates over `gateRampSeconds = 0.002`, and slice or phase jumps cross-fade through up to two overlapping
  fades of 3 ms (`RhythmEngine.h`). History is bounded to 4 s. There is no longer-duration scene fade. C04's per-scene smoothing time is not implemented (owner W3).
- Duplicating onto the SOUNDING scene while the transport plays therefore changes the sound with a 3 ms ramp per parameter, not a musical fade.

## 3. Operation overrides (new host parameters, owner W10)

Design rule: automation must never rewrite scenes. The new parameters are a LIVE LAYER applied to the effective settings after morph and before placement gating
(`GrooveMorphAudioProcessor::effectiveSettings`). Neutral values reproduce today's sound exactly, so old projects are unchanged.

Operation order and names follow `OperationType`: `Repeat` (0), `Reverse` (1, enum ReverseSlice), `Gate` (2), `Halftime` (3, enum HalfTime), `Tapestop` (4, enum TapeStop). IDs are camelCase like the existing ones (`fxGateDepth`). Never reorder or rename.

| ID (X = Repeat, Reverse, Gate, Halftime, Tapestop) | Type | Range | Default (neutral) | Meaning |
|---|---|---|---|---|
| `fx<X>Mode` | Choice | "Scene", "Off", "On" | Scene | Scene = use the sounding scene; Off forces disabled; On forces enabled (depth taken from the scene, or 1.0 when the scene value is 0) |
| `fx<X>Depth` | Float | 0..1 | 1.0 | Multiplies the scene depth (and the event depth) |
| `fx<X>Slice` | Float | -2..+2 octaves, step 0.01 | 0 | `slice = clamp(sceneSlice * 2^shift, 1/16, 4)` meter beats; event-local slices (when set) are shifted the same way |

Fifteen parameters, `ParameterID` version hint 1, appended AFTER all existing parameters and after those of W2/W3/W8 in merge order (W10 resolves ordering at integration; ids never move once released).
Display names: "Repeat Mode", "Repeat Depth", "Repeat Slice", and so on.

Interaction rules:
- Activity limit (C06): the `enabledMask` passed to `evaluate()` is computed AFTER the override, so a forced-On effect competes by priority.
- C03 locks protect scene fields from edits; they do not block automation (automation is not an edit).
- Morph (F02) is applied first, overrides second.
- Smoothing: values are read once per block; the engine's 3 ms ramps handle steps. No extra smoothing is added.
- MIDI links: `p:fx<X>Depth` etc. become valid link targets (`MidiLinkTable::validTarget` accepts any `p:<id>` of 3..64 printable characters; the processor must check the ID exists). Existing `s:` targets keep working. NOTE for W2: `validTarget` accepts `s:` effect digits only `0`..`4` (and `applyLinkedValue` checks `effect >= operationCount`); extend both together if the operation count grows.
- W2's new effects append their own overrides using the same naming scheme (`fxFiltersweepMode` and so on, names set in N01); the operation count grows from 5 to 7 only through W2's approved change.

## 4. State keys

APVTS parameters only. No scene keys, no format change. Older projects lack the parameters and restore them to the neutral defaults (`setStateInformation` already resets absent parameters).

## 5. Real-time and threading

The audio thread reads fifteen cached atomics once per `process()` call (`ParameterCache` additions) and applies them to a stack copy of `VariationSettings`. No allocation, no locks. The `effectiveSettings` result is
already computed per segment; overrides must be applied in the same function so the evaluator and engine see one consistent view.

## 6. Whole-scene fade policy (decision)

1. A whole-scene replacement while playing (duplicate, reset, history recall committed to the sounding scene, undo/redo, Apply) uses the SAME engine ramps as any parameter change. No extra fade is added by default.
2. When W3 lands C04 per-scene smoothing, replacement of the SOUNDING scene uses the destination scene's smoothing time; value 0 means the engine ramps only.
3. A replacement must never produce a click louder than a normal 3 ms ramp: tested with a rendered fixture (A06-T06).
4. Scene SWITCHES (Scheduler) are governed by the transition mode and C04; they are not changed by this spec.
5. Copy FX and duplicate remain refused during audition so preview/apply ownership stays unambiguous.

## 7. UI wiring

No new tab. The Perform tab may show the live layer as a compact strip (Mode / Depth / Slice per effect) bound with attachments; any layout is W10's. All controls use the `Menu*` widgets so right-click learn works.

## 8. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| A06-T01 | Copy FX copies settings only, undoable, refused during audition | EXISTS | `tests/PluginProcessorTests.cpp` "copy A effects into B", "processor copy can undo", "copy blocked during audition"; `tests/core/SceneBankTests.cpp` "copy preserves target name" |
| A06-T02 | Whole-scene duplicate/reset keep the destination's locked fields; undoable | EXISTS | PluginProcessorTests "duplicate keeps the destination's locked depth", "reset keeps locked fields", "duplicate undo"; SceneControlTests "duplicate scene", "reset scene" |
| A06-T03 | The 15 override parameters exist with the exact IDs/ranges/defaults above and survive a state round trip; legacy state restores neutral | MISSING, owned by W10 | `tests/PluginProcessorTests.cpp` + legacy fixture |
| A06-T04 | Neutral overrides render bit-identically to the current engine output | MISSING, owned by W10 | processor render test |
| A06-T05 | Mode On/Off, Depth scale and Slice shift change the render as specified; scenes and undo stack untouched | MISSING, owned by W10 | processor test |
| A06-T06 | Replacing the sounding scene during playback: maximum sample-to-sample step of the output stays within the bound of an equal-length steady render of the louder scene (record the bound with the fixture) | MISSING | processor render test |
| A06-T07 | Overrides participate in the activity limit and are applied after morph | MISSING, owned by W10 | processor test |
| A06-T08 | pluginval strictness 5 still passes with 15 more parameters | CI | `.github/workflows/pluginval.yml` |
| A06-T09 | Native host: automation lane for `fxGateDepth` records and plays back in REAPER | MISSING (manual, issue #19) | QA ledger |

## 9. Non-goals

Automating scene contents (events, regions), per-scene automation lanes, changing the scene file format, renumbering existing parameters.
