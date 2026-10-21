# W3-MORPH: scene morph of placement, events and regions (F02)

Status: SPEC (2026-10-20, base `c83e8b2`). Implements section 8 of [F02](F02-SCENE-MORPHING.md) (written by W1 on its branch; the text is the contract) with the refinements below.
Deviations from F02 section 8 are marked DEV and are recorded in `docs/coordination/DECISIONS.md`.

## 1. Objective

`morph(a, b, w)` returns the settings that SOUND while the morph amount is `w` (0 = scene a, 1 = scene b): effect rows as before, plus placement, regions, activity limits and lane events of BOTH scenes.
It is deterministic, allocation-free, never edits stored scenes, and identical to today for scenes without control data.

## 2. Contract

`VariationSettings morph(const VariationSettings& a, const VariationSettings& b, float weight, std::int64_t phraseTicks = 0) noexcept`
`weight` is clamped to [0,1], non-finite -> 0. `weight <= 0` returns `a` verbatim. `phraseTicks` is the current phrase length in ticks (the processor passes it); 0 disables phrase-dependent steps (overlap checks use linear intervals,
legacy-uniform sides contribute no events).

### 2.1 Effect rows
Unchanged (both on: interpolate; one side: depth scaled by its side's weight; neither: `a`).

### 2.2 Control data
1. If `sameControl(a.control, b.control)`: control from `a`, nothing else to do (so scenes without control data are bit-identical to the old behaviour).
2. Discrete placement fields (`enabled`, `window`, `every`, `cadenceOffset`, `originTicks`, `subdivision`, `downbeatProtectBeats`, `regionCount`, `regions`) and the activity limits: from `a` while `w < 0.5`, from `b` when `w >= 0.5`.
3. `startPhase`, `spanPhase`: linear. `loopBeats` per operation: from `a` while `w < 0.5`, else `b`.
4. DEV: `phase` (C04 launch data) always from `a`: the processor reads launch phase from the STORED destination scene, and a launch never happens through a morph. Permissions (locks) from `a`.
5. Locks never apply to a morph: the result is the sounding blend, stored scenes are not touched (F02 section 8.5). Locked flags of events are copied, they have no effect on sound.
6. `explicitEvents = a.explicit || b.explicit`. A non-explicit side contributes one full-phrase event (onset 0, duration `phraseTicks`, depth 1, slice 0, priority `activity.priority[op]`) per ENABLED operation of that scene
   (the `convertLegacyToEvents` rule), when `phraseTicks > 0`.
7. Events (disabled events are ignored). Side-a events are processed in array order, side-b likewise:
   - MATCHED: same `(operation, onsetTicks, durationTicks)`; each a-event matches the first unmatched b-event. Result: depth linear, priority linear (rounded), slice log2-linear when both are non-zero
     else from the dominant side (`w < 0.5 ? a : b`), `restartCycle` from the dominant side, `enabled` true.
   - A-ONLY: depth `a.depth * (1 - w)`; B-ONLY: depth `b.depth * w`. Events whose depth reaches 0 are dropped (so at w = 1 no a-only event remains, at w just above 0 b-only events are almost silent).
   - Order of insertion (deterministic): matched events (a order), then unmatched events of the dominant side (array order), then unmatched events of the other side.
     An event is skipped when it overlaps (same operation, on the merged lane ring: `loopBeats[op]` beats or the phrase) an event already inserted: this resolves same-operation overlap in favour of `w < 0.5 ? a : b`.
   - Insertion stops at `maxLaneEvents` (256); the remaining events are dropped in that order.
8. No dynamic memory: fixed index arrays on the stack (about 1.5 KB).

## 3. Processor integration

`effectiveSettings(variation)` returns `const VariationSettings&`: the snapshot's scene when no morph applies (target off, `w <= 0`, or target equal to the scene), otherwise a CACHED morph result held in a preallocated member.
The cache key is `{variation, target, weight bits, snapshot version, phraseTicks}`; the result is rebuilt on the audio thread only when the key changes (once per block while the amount is automated).
The reference stays valid for the duration of one `renderSegment` call. Cost: O(events^2) per rebuild (256 events: about 65 k comparisons), no allocation.

## 4. Acceptance tests (`tests/core/MorphControlTests.cpp`; processor cases in `tests/W3ProcessorTests.cpp`)

| ID | Check |
|---|---|
| F02-T08a | `w = 0` returns `a` exactly; NaN / negative / > 1 weights are safe; scenes without control data give exactly the old result for every `w`. |
| F02-T08b | `w = 1`: placement/regions/activity/loop equal `b`'s; the event SET equals `b`'s enabled events (depth `b.depth`); a-only events are gone. |
| F02-T08c | Monotonic: an a-only event's depth never rises with `w`, a b-only event's never falls; matched depth lies between the two. |
| F02-T08d | Threshold: at `w = 0.49` discrete fields are `a`'s, at `0.5` they are `b`'s; regions follow. Start/span linear. |
| F02-T08e | Overlap: an a-event and an overlapping b-event of the same operation never both appear; `w < 0.5` keeps a's, `>= 0.5` keeps b's. |
| F02-T08f | Legacy-uniform side vs explicit side: legacy contributes full-phrase events per enabled op; both legacy: lane stays non-explicit. |
| F02-T08g | Capacity: 256 + 256 distinct events give <= 256 events, deterministic order; same call twice gives bit-identical results. |
| F02-T08h | Pure function: inputs unchanged (copy compare); locks have no effect; output passes `sanitize()` without change. |
| F02-T06 | Processor: morph target equal to the sounding scene is a no-op. |
| F02-T07 | Processor: stored scenes and undo stack unchanged by a morph. |
| F02-T09 | Processor: render with a morph is block-split independent (blocks 64/257/1024 give identical output). |
| W3-MO-T01 | Processor: morphing A (no control) toward B (explicit Gate event in the second half) at 1.0 makes the gate act only in that half. |

## 5. Non-goals

Editor preview of the morph result; per-sample smoothing of the amount; morphing more than two scenes; baking the blend into a scene.
