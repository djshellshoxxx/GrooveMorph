# F01 Controlled variation generation

Status: IMPLEMENTED (subset), spec COMPLETE. Written 2026-10-20 against `c83e8b2`. Replaces the INCOMPLETE draft
`docs/expansion/features/F01-VARIATION-GENERATOR.md` (unmerged PR #6, branch `spec/groovemorph-expansion-v1`).
Cross-refs: [C02](../control-specs/features/C02-VARIATION-DISTANCE.md), [C03](../control-specs/features/C03-CHANGE-PERMISSIONS.md),
[F08](F08-VARIATION-HISTORY.md).

## 1. Objective

Offer deterministic, reproducible variations of a stored scene that the user previews, then applies or reverts, without
ever touching the stored scene or a locked field until Apply. A candidate is a pure function of
`(source VariationSettings, GeneratorSettings)`.

## 2. As implemented

Pipeline (`src/core/variation/CandidateBuilder.cpp`, `buildCandidate`):

1. `variation::generate(Request)` (`VariationPlanner.cpp`): SplitMix64 seeded from `seed`, then advanced by
   `candidateIndex * 0xd6e8feb86659fd93`. Exactly five random draws per operation are consumed whether or not the operation
   is selected or protected, so protecting one operation never changes another. Per-operation selection probability by style:
   Conservative 0.25, Creative 0.55, Experimental 0.85. Depth spread: 0.12 / 0.30 / 0.55. Slice is replaced from
   `{1/16,1/8,1/4,1/2,1,2,4}` with probability 0.15 (Conservative) or 0.5. Enable flip probability 0.22 (Experimental) else 0.05.
2. `control::blendDistance(source, planned, distance)` (`VariationBlend.cpp`): C02 distance. 0 returns source, 1 returns
   candidate exactly, depth linear, slice log2-linear, enable flips only when `distance >= 0.5`.
3. `control::enforcePermissions(source, candidate)` (C03): locked fields are restored AFTER generation, so locks never
   perturb the random sequence. Returns the restore count (`Candidate::permissionRestores`).
4. Optional `generateEventPatterns` (`GeneratorSettings::generateEventPatterns`, default false): replaces the unlocked part
   of the event lane with a deterministic pattern. Skipped when `placementLocked`, `distance <= 0` or non-finite. Kept
   exactly: events with `locked`, events of operations whose Events lock bit is set, events of "never touch" operations,
   and `lane.loopBeats`. Events per enabled operation: `max(1, round(styleCount * distance))` with styleCount 1/2/3.
   Onsets snap to `placement.subdivision`; durations from `{1/4, 1/2, 1}` beat; depth `0.45 + 0.55*u`; slice from
   `{0,1/8,1/4,1/2,1}`; priority copied from `activity.priority[op]`; `restartCycle` only for Experimental (1 in 4).
   Up to 64 placement attempts per event; candidates overlapping a protected region or an existing same-operation
   event are skipped. Capacity bound is `maxLaneEvents` (32 today, 256 after W3).
5. `scene::sanitize`, then `changedOperations` bitmask and `changedEvents` flag are computed.

`GeneratorSettings` fields: `seed` (u64, default 1), `style` (Conservative|Creative|Experimental), `distance`
(0..1, default 1), `candidate` (u32, regenerate = +1), `protectedOperations` (bitmask, never-touch), `generateEventPatterns`,
`phraseTicks` (default 4 bars of 4 beats; clamped to 960 .. 16*32*960 ticks).

Non-finite or out-of-range source floats in non-selected operations are preserved bit-exactly (`VariationPlanner.h`
contract); selected operations are normalised to depth 0..1, slice 1/16..4 (fallbacks 0.5 and 0.25).

## 3. Parameter IDs, ranges, defaults

None are host parameters. All generator controls are editor/session state. Scene seed is `SceneBank::seed()`
(default `0x47524F4F56454D31`, serialized as `seed`). Generator history file: see F08.

## 4. State keys

No new keys. Applied candidates become ordinary scene keys (`variation.<i>.op.*`, `variation.<i>.lane.*`,
`variation.<i>.event.*`; see `SceneSerializer.cpp`). The scene format stays `groovemorph-scenes v1`.

## 5. Real-time and threading

`buildCandidate` is `noexcept`, allocation-free and bounded (operationCount (7) operations, at most 64 attempts per event,
at most 32 events today). It runs on the message thread when the user presses a Gen tab button. It is never called from the
audio callback. Preview goes through `AuditionSession`/`publishSceneLocked` (snapshot handoff with SpinLock try-lock,
see `PluginProcessor.h` header comment), so the stored bank is untouched until Apply, which is one undoable commit.
The draft's cancellable background worker is NOT implemented and is not required: the work is microseconds-scale and bounded,
so a worker would only add lifecycle risk (decision recorded in section 8).

## 6. UI wiring (Gen tab, `src/ui/InspectorPanel.cpp` GeneratorTab)

Style combo, Seed + "New seed", Distance slider, "Generate preview", "Generate 3 options" (gallery Option 1..3), `<` / `Regenerate >`,
"Generate event patterns" toggle, "Never touch" per-operation toggles, lock grid (Enable/Depth/Slice/Events per operation,
"Lock placement"), generation history combo. Apply/Revert live in the main editor audition row. Every control carries a tooltip
and routes through `finishControlMenus` (right-click value entry / MIDI learn where a target exists).

## 7. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F01-T01 | Same source+settings gives identical candidate; event generation deterministic | EXISTS | `tests/core/CandidateBuilderTests.cpp` "same inputs same candidate", "event generation is deterministic" |
| F01-T02 | Different candidate index / seed varies output; no (seed,candidate) aliasing | EXISTS | CandidateBuilderTests "candidates vary", "event candidates vary", "seed and candidate index no longer alias" |
| F01-T03 | Distance 0 = source, 1 = planner candidate, intermediate blends | EXISTS | CandidateBuilderTests "distance 0 = source"; SceneControlTests "distance 0 identity", "distance 1 candidate", "half depth", "half slice geometric" |
| F01-T04 | Locks, never-touch, fully locked scene unchanged; protection keeps independent draws | EXISTS | CandidateBuilderTests "fully locked scene unchanged", "protected unchanged"; VariationPlannerTests "protection keeps independent random draws" |
| F01-T05 | Invalid/non-finite input bounded; unselected invalid fields preserved | EXISTS | `tests/core/VariationPlannerTests.cpp` "selected invalid input becomes finite and bounded", "unselected invalid fields preserved exactly" |
| F01-T06 | Locked events, loop lengths, protected regions, placement lock, capacity honoured by event generation | EXISTS | CandidateBuilderTests "locked event retained exactly", "lane loop lengths retained", "generated events avoid protected region", "placement lock freezes event generation", "generated events fit capacity" |
| F01-T07 | Preview never modifies the bank; Apply is one undo step; Revert restores | EXISTS | `tests/core/SceneUndoAuditionTests.cpp` "bank untouched by audition", "commit undoable"; PluginProcessorTests "audition does not touch bank", "audition commit" |
| F01-T08 | Golden fixture: fixed seed/style/candidate/source yields a literal expected settings + event list, identical on GCC, Clang, MSVC | MISSING | add `CandidateBuilderTests` "golden candidate" with literal values; CI matrix already builds three compilers |
| F01-T09 | Capacity 256: generation never exceeds the new `maxLaneEvents` | MISSING (depends on W3 capacity change) | extend "generated events fit capacity" |
| F01-T10 | Experimental style only sets `restartCycle`; Conservative never does | MISSING | new CandidateBuilderTests case |
| F01-T11 | GUI wiring: Generate/Apply/Revert buttons exist, drive processor, tooltips present | MISSING (JUCE UI cannot be built locally) | `tests/PluginProcessorTests.cpp` editor section |

## 8. Gaps and owners

- Golden cross-compiler fixture (F01-T08): unowned, W1 follow-up (test only, needs a compiled run).
- Event pattern generation is a phrase-level first pass (random onsets, three durations). Musical intelligence
  (accent preservation, density scoring from the draft) is NOT implemented. Euclidean/chance generation: owned by W6 (N03).
- Capacity 256 events: owned by W3.
- Candidate persistence: see F08.

## 9. Non-goals

Cloud or ML generation. Cancellable background worker (see section 5). Generating scenes that bypass C03 locks. Editing
scenes other than the one being previewed. Per-event phase restart authoring beyond `restartCycle`.
