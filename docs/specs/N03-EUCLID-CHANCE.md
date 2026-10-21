# N03 Euclidean patterns and per-event chance

Status: implemented on branch of worker W6 (Beta workplan 2026-10-20). Core is portable and tested; the JUCE
panel is written against JUCE 8.0.7 and is NOT compiled locally (CI compiles it).

## 1. Objective

1. **Euclid generator.** Fill one operation's event lane with a Euclidean rhythm (Bjorklund) on a chosen
   subdivision grid, as a deterministic, previewable, single-undo action that obeys every existing lock.
2. **Chance.** Every event, and every operation lane, can fire with a probability of 0..100 % per cycle of the
   lane. The decision is a pure hash, so playback is reproducible, independent of host block size, and needs no
   RNG state on the audio thread.

Non-goals: swing/humanize, per-step velocity editing, polymetric Euclid layers, Euclid over several operations at once,
chance on legacy (uniform, non-event) scenes, saving a random "re-roll" counter in the project.

## 2. Euclid generator

Files: `src/core/control/EuclidPattern.{h,cpp}` (portable, fixed capacity, no allocation).

### Parameters (`EuclidParams`)

| Field | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `steps` | u8 | 1..32 (`euclidMaxSteps`) | 8 | Pattern length in grid steps |
| `pulses` | u8 | 0..steps | 3 | Number of hits |
| `rotation` | i8 | any, taken modulo `steps` | 0 | Positive rotation moves the hits later: `out[i] = base[(i - rotation) mod steps]` |
| `operation` | u8 | 0..laneCount-1 | 0 | Target operation lane |
| `subdivision` | u8 | 1,2,3,4,6,8,12,16 per meter beat | 4 | Step length = `960 / subdivision` ticks (exact) |
| `gatePercent` | u8 | 10..100 | 50 | Event length as a percentage of one step |
| `repeat` | bool | | true | Tile the pattern across the lane ring (phrase, or the operation's loop length) |
| `accentEvery` | u8 | 0..32 | 0 | 0 = off. Else every Nth pulse of a tile (pulse 0, N, 2N, ...; counted again in every tile) is an accent |
| `accentDepth` | f32 | 0..1 | 1.0 | Event depth of accents (and of all pulses when accents are off) |
| `softDepth` | f32 | 0..1 | 0.6 | Depth of non-accent pulses when accents are on |
| `density` | u8 | 0..100 | 100 | Chance (section 3) given to NON-accent events (to every event when accents are off) |

`sanitize(EuclidParams&)` clamps all fields (NaN depths become the default).

### Algorithm

`bjorklund(steps, pulses)` returns the canonical Toussaint/Bjorklund pattern starting with a hit at step 0
(group-merging algorithm on 32-bit masks; no heap). Known results, required by the tests (x = hit):

- E(3,8) = `x..x..x.`; E(5,8) = `x.xx.xx.`; E(7,16) = `x..x.x.x..x.x.x.`
- E(2,5) = `x.x..`; E(3,4) = `xxx.` (standard Bjorklund output, a rotation of Toussaint's `x.xx`; use Rotation for that form); E(4,9) = `x.x.x.x..`; E(5,12) = `x..x.x..x.x.`
- E(0,n) is empty, E(n,n) is all hits, E(1,n) is a single hit at step 0.

### Placement

Tile `t` (0..) step `s` places an event at `onset = (t*steps + s) * stepTicks` while `onset < ring`, where
`ring` = phrase ticks, or `loopBeats[op]*960` when that operation has its own loop length. With `repeat` off
only tile 0 is placed. Duration = `max(1, stepTicks*gatePercent/100)` clipped to `ring - onset`. New events get
`priority = activity.priority[op]`, `restartCycle = false`, `enabled = true`, `locked = false`.

### Application rules (`applyEuclid`, `buildEuclidCandidate`)

1. Refused (`EuclidStatus::OperationLocked`, lane untouched) when the operation has the **Events** lock bit.
2. If the lane is legacy-uniform (`explicitEvents == false`), the OTHER enabled operations are first converted to
   full-phrase events (`convertLegacyToEvents`) so their sound does not disappear, then `explicitEvents = true`.
3. Existing UNLOCKED events of the target operation are removed (Apply is idempotent); event-locked events stay.
4. Steps are placed through `addEvent`, so capacity (`maxLaneEvents`, never a literal), overlap (also against
   locked events) and validity rules are the existing ones. A pulse is skipped, and counted in the report, when it
   (a) overlaps a protected region, (b) overlaps another event, or (c) would exceed lane capacity (placement then
   stops). Placement locks are never altered: only `lane.explicitEvents` and `lane.events` change.
5. `buildEuclidCandidate(source, params, phraseTicks)` works on a copy of the source scene and finally calls
   `enforcePermissions(source, candidate, true, phraseTicks)`, the same path as manual edits and the generator.
   The UI then hands the candidate to the editor, which enforces permissions again (idempotent).
6. `clearEuclidOperation(...)` removes all unlocked events of the operation (one action; locked events stay).

### UI behavior

- Preview: `SceneEditAccess::previewCandidate(candidate)` (existing AuditionSession). Nothing is stored until the
  user presses the editor's existing "Apply preview".
- Apply to lane: `SceneEditAccess::setSettings(candidate)`: one undoable committed edit (or an edit of the running
  preview, like every other Place edit).
- Clear lane: same path with `clearEuclidOperation`.

## 3. Chance

### Data (additive, optional v1 scene keys)

| Struct field | Range / default | Scene key | Written when |
|---|---|---|---|
| `LaneEvent::chance` | u8 percent 0..100, 100 | `variation.V.event.K.chance` | value != 100 |
| `EventLane::chanceByOp[op]` | u8 percent 0..100, all 100 | `variation.V.lane.chance.OP` | value != 100 |
| `EventLane::chanceSeed` | u32, 0 | `variation.V.lane.seed` | value != 0 |

Legacy files lack the keys and load as 100 % / seed 0, and scenes that do not use chance serialize byte-identically
to earlier builds. Out-of-range values are clamped. The scene format stays `v1`.

"Per lane" means per operation lane (the five rows of the Place canvas). The probability that an event fires is
`event.chance * chanceByOp[op]` in units of 1/10000.

### Decision (`core/control/Chance.h`, header-only, constexpr)

```
h = mix64(seed, op, onsetTicks, cycle)        // splitmix64 finalizer chain, pure function
fires = ((h >> 32) % 10000) < chance*laneChance
```

- `seed` = `EventLane::chanceSeed` (stored per scene). Deviation from the brief ("seed, scene, op, event, cycle"):
  the scene is folded into the stored seed; `ensureChanceSeed(lane, sceneIndex)` assigns a non-zero, scene-specific
  seed the first time chance is used, so scenes roll differently without the evaluator needing a scene index
  (the planner and Visual window do not have one).
- `event identity` = `onsetTicks` (an event keeps its dice when other events are added or removed; same-operation
  events cannot share an onset on one ring).
- `cycle` = `floor((patternTicks - onsetTicks mod ring) / ring)`: the lane-ring repetition the event belongs to
  (phrase for ordinary lanes, loop length for looped lanes). An event that wraps past the ring end decides once, in
  the cycle that contains its onset. Pattern position is used (after the C04 phase shift and origin), so
  "Restart scene" replays the same sequence from cycle 0.
- 100 % never hashes (fast path, identical to legacy); 0 % never fires.
- A suppressed event behaves exactly like a disabled one: it does not claim the operation, so it does not consume an
  activity-limit slot. Segment boundaries (`ticksUntilChange`) are unchanged, so block splitting is unaffected.

### RT behavior

`evaluate()` stays `noexcept`, allocation-free and loop-bounded. The hash is a pure function of its arguments; no
state is stored, so the result is identical for any block size, MIDI split, or evaluation order.

### Where chance is honoured

- `PlacementEvaluator::evaluate`: audio, and therefore also `PlanBuilder` (planned path) and the Visual window rings,
  which call the same function. No change is needed in those files.
- Preserved by `scaleEvents`, `reduceEvents`, generator pattern replacement (lane chance/seed carried over),
  `enforcePermissions` (chance is part of an event's identity; locked events keep their chance; lane chance of a
  protected operation is restored), morph (control taken from scene A) and copy/duplicate (whole-struct copy).
- Place canvas: events with an effective chance below 100 % are drawn with diagonal hatching and their effective
  percentage. The Visual window's rings use `evaluate()` and therefore show the real (chance-resolved) activity; there is
  no separate hatching there.

### UI (`src/ui/EuclidPanel.{h,cpp}`)

Embedded in the Place tab behind a "Euclid / chance" toggle that swaps it in over the lower event-slider rows
(the Place tab has no vertical room left). Contains: operation, grid and length (25/50/75/100 % of a step) selectors, steps/pulses/rotation sliders,
accent-every and density, a ring preview of the pattern (steps on a circle, hits filled, step 0 marked),
Preview / Apply / Clear buttons, a status line, and the chance sliders for the selected event and for the chosen
operation lane. The ring preview and chance edits show what will be written; chance edits are committed on release
like the other Place sliders (one undo step).

## 4. Acceptance tests (`tests/core/EuclidChanceTests.cpp`)

| ID | Check |
|---|---|
| N03-T01 | Bjorklund matches the known rhythms listed in section 2 and the degenerate cases |
| N03-T02 | Pulse count equals `pulses` for every `steps` 1..32, `pulses` 0..steps; maximal evenness (gaps differ by at most 1) |
| N03-T03 | Rotation shifts hits later, wraps, is periodic in `steps`, negative rotation works |
| N03-T04 | Applying E(3,8) on a 1/8 grid tiles over a 4/4, 4 bar phrase at exact ticks; durations follow `gatePercent` |
| N03-T05 | Apply is deterministic and idempotent (second apply equals first); `repeat` off places one tile only |
| N03-T06 | Capacity: a dense pattern stops at `maxLaneEvents`, reports skipped pulses, never exceeds capacity |
| N03-T07 | Events lock on the operation refuses; per-event locked events survive and block overlapping pulses |
| N03-T08 | Pulses inside a protected region are skipped; placement lock/config unchanged |
| N03-T09 | Legacy-uniform scene: other enabled operations are converted, target gets only the Euclid events |
| N03-T10 | `buildEuclidCandidate` equals `applyEuclid` + `enforcePermissions` (idempotent through enforcement) |
| N03-T11 | Accent pattern: accents at pulse 0,N,.. have `accentDepth`; others `softDepth` and density chance |
| N03-T12 | Chance hash is deterministic and sensitive to each input; 100 % always, 0 % never |
| N03-T13 | Playback invariance: evaluating a long span in different block splits and orders gives identical activity |
| N03-T14 | Statistics: over 20000 cycles, hit rate for 25 %, 50 %, 80 % and for event x lane (50 % x 50 % = 25 %) is within 2 % absolute (seeded, deterministic) |
| N03-T15 | Different seeds give different sequences; the same seed reproduces the same one |
| N03-T16 | Loop-ring lane uses the loop cycle index; wrap-around event decides once per cycle |
| N03-T17 | Suppressed events do not consume activity-limit slots |
| N03-T18 | Serialization round trip of chance/seed; legacy file loads as 100 %; default-chance scenes serialize without the new keys; garbage values clamp |
| N03-T19 | `scaleEvents`, `reduceEvents`, `enforcePermissions` keep chance and lane chance; locked event chance cannot be changed by an edit |
| N03-T20 | Planned path (`buildPlanSegments`) honours chance exactly like `evaluate` |

Not testable locally (no JUCE): EuclidPanel layout/painting, Place-tab hook, canvas hatching, tooltips. Recorded
as unverified; CI compile and a manual UI pass are required.
