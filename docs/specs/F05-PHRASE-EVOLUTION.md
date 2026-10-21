# F05 Phrase evolution

Status: IMPLEMENTED (single scene; step prepared on the message thread and applied on the audio thread at the phrase boundary, W3-TIMER-EVOLUTION). Beta audit: processor-level rows open (TODO B-02). Updated 2026-10-20 by the A1 completeness audit.
Replaces draft `docs/expansion/features/F05-PHRASE-EVOLUTION.md` (PR #6); the draft's multi-cycle transition graph and lineage
commit/rollback are NOT implemented and are listed as deferred.


## Beta audit status (2026-10-20)

- Built: evolution never edits the stored bank; settings (not the step counter) are saved with the project.
- Open: processor tests for locks per step, audition suppression, not-the-sounding-scene and seek-back (F05-T06..T08, T10, B-02); multi-scene evolution and step preview are not built (F05-T09, B-06).
- Acceptance rows below carry audited statuses; the full join of ID, test, implementation and UI wiring is in [COMPLETENESS-MATRIX.md](COMPLETENESS-MATRIX.md).

## 1. Objective

Every N phrases the chosen scene SOUNDS as a different deterministic variation of its stored settings, so a loop keeps developing.
Stored scenes, undo history and saved scene text are never changed by evolution.

## 2. As implemented

State (processor, guarded by `modelMutex`): `evolutionOn`, `evolutionScene` (0..3), `evolutionEvery` (1..16), `evolutionSettings`
(`variation::GeneratorSettings`: `distance` 0..1, `style` 0..2, fixed `seed = 0x47524F4F56`), `evolutionStep`, `lastEvolutionPhrase`.

- `setEvolution(enabled, scene, everyPhrases, distance, style, seed)` clamps inputs, resets `evolutionStep = 0`, sets
  `lastEvolutionPhrase` to the current phrase index and republishes the scene.
- `updateEvolution()` runs from the processor Timer (20 Hz, message thread). While enabled and the transport plays it reads
  `phraseIndexStatus` (floor of the position in phrases, written by the audio thread); on a phrase change with
  `phrase mod every == 0` it increments `evolutionStep` and republishes. Therefore the first change happens at the first qualifying phrase
  boundary AFTER enabling, never within the first phrase.
- `publishSceneLocked()` replaces `snapshot.variations[evolutionScene]` by `buildCandidate(storedSettings, {seed, style, distance,
  candidate = evolutionStep - 1})` when `evolutionOn && !auditioning && evolutionStep > 0`. Locks (C03) are honoured by `buildCandidate`.
  The audio thread picks the new snapshot at the next block start (`pullScene`, SpinLock try-lock).
- Timing consequence: a step is applied up to the timer period (about 50 ms) after the phrase boundary, plus one block. This is a
  KNOWN LIMITATION; sample-accurate evolution is not provided.
- Reset: `resetToDefaults()` switches evolution off; `setStateInformation` restores via `setEvolution(...)` with the saved flags and a fresh counter.

## 3. Parameters

None are host parameters (not automatable). Ranges: every 1..16 phrases; distance 0..1 (default 0.3 when restoring a project without the value);
style 0 Conservative, 1 Creative, 2 Experimental; scene 0..3.

## 4. State keys

Project state child `Evolution` (`PluginProcessor.cpp` `evolutionTreeType`): properties `enabled` (bool), `scene` (int), `every` (int),
`distance` (double), `style` (int). Absent in older projects -> disabled with defaults. The seed is fixed in code, is not saved and has
no UI. The step counter is not saved.

## 5. Real-time and threading

No audio-thread work beyond reading the published snapshot. All evolution logic runs on the message thread under `modelMutex`.
`buildCandidate` is bounded and allocation-free. Owner W10 reviews timer-thread safety.

## 6. UI wiring

Perform tab: "Phrase evolution (sounding only, never edits scenes)": "Evolve scene" toggle, scene selector, "Every N phrases" slider (1..16),
"Distance" slider (0..1; UI defaults: every 2, distance 0.3, style Conservative, scene A), style combo, status line (`getEvolutionStep()`).

## 7. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F05-T01 | No step within the first phrase; steps at the next qualifying boundary and again after `every` phrases | EXISTS | `tests/PluginProcessorTests.cpp` "no evolution step within the first phrase", "evolves at the phrase boundary", "and again next phrase" |
| F05-T02 | Stored scene and bank untouched; undo stack unaffected | EXISTS | PluginProcessorTests "stored scene is untouched", "evolution does not modify the saved scene bank" |
| F05-T03 | Evolution off restores the stored sound | EXISTS | PluginProcessorTests "evolution off" |
| F05-T04 | Settings restore from a project; fresh processor has none | EXISTS | PluginProcessorTests "evolution restored", "a fresh processor has no evolution" |
| F05-T05 | Sequence reproducible: same stored scene + settings + step gives identical candidate | EXISTS (via builder determinism) | CandidateBuilderTests "same inputs same candidate" |
| F05-T06 | Locks respected by every step | DEFERRED | Locks are asserted at builder level (CandidateBuilderTests F01-T04); evolution reuses buildCandidate. No evolution-level assertion. Processor-level test needs the JUCE build, which the core-only authoring machine cannot run; open as TODO B-02. |
| F05-T07 | Auditioning a scene suppresses evolution for the preview | DEFERRED | Processor-level test needs the JUCE build, which the core-only authoring machine cannot run; open as TODO B-02. |
| F05-T08 | Evolving scene is not the sounding scene: step still happens, no audio effect until the scene sounds | DEFERRED | Processor-level test needs the JUCE build, which the core-only authoring machine cannot run; open as TODO B-02. |
| F05-T09 | Preview of upcoming steps | DEFERRED | Preview of upcoming steps is not built. Open as TODO B-06. |
| F05-T10 | Loop-aware cadence on seek: a seek backwards does not skip or double-fire steps | DEFERRED | Seek-back across a boundary is implemented by the W3 timer/plan design but not asserted. Processor-level test needs the JUCE build, which the core-only authoring machine cannot run; open as TODO B-02. |

## 8. Gaps

Unowned, deferred: evolving more than one scene; preview of upcoming steps; sample-accurate step application (move the step into the
audio thread against `phraseIndex`); saving the seed; progression profiles and lineage commit/rollback from the draft. W3's capacity
change (256 events) affects only the builder.

## 9. Non-goals

Rewriting stored scenes ("bake" is a separate explicit command and is not specified). Cross-phrase scoring (density/continuity).
