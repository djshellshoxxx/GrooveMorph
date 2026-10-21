# W3-LOOP-REDUCE: loop-aware Reduce / Analyze (C06, F06)

Status: SPEC (2026-10-20, base `c83e8b2`). Closes "Reduce still works on the phrase ring" (DECISIONS) and F06-T09.

## 1. Objective

`reduceEvents` (Limit tab: Analyze / Reduce) judges spacing, coverage and peak concurrency on the TIMELINE the evaluator actually plays: an operation with `loopBeats[op] = L > 0`
repeats its events every `L * 960` ticks, independent of the phrase. Lanes with `loopBeats = 0` behave exactly as before (ring = phrase), bit-identical decisions.

## 2. Definitions

- Ring of an event: `R(op) = loopBeats[op] > 0 ? loopBeats[op] * 960 : phraseTicks`. Onset is taken modulo `R`, duration is capped at `R` (the evaluator's own rule).
- Unrolled spans: copies of an event starting at `(onset mod R) + k*R` for every integer k with start < phraseTicks, including k = -1 when the event wraps past `R`
  (a copy that begins before tick 0 and reaches into the window). Spans are half-open `[start, start + duration)` clipped to `[0, phraseTicks)`.
  The analysis window is the first phrase of the timeline (pattern position 0..P). If P is not a multiple of a lane's ring, later phrases can differ; the report describes phrase one.
- Coverage: length of the union of all unrolled enabled spans of the kept set divided by `phraseTicks`.
- Spacing: the minimum distance between two event starts. For events i, j with rings `Ri`, `Rj` the closest pair of their repetitions is at distance `d = min(m, g - m)`,
  `m = (onset_i - onset_j) mod g`, `g = gcd(Ri, Rj)` (exact, integer). For i == j this reduces to the ring itself; it is applied for a looping lane only (`R < phraseTicks` by choice of the user):
  an event whose own repetition period is shorter than `minSpacingBeats` violates by itself (dropped, or ConflictLocked when mandatory). For non-looping lanes the self distance is not checked (legacy).
- Peak concurrent: the maximum number of distinct operations active at one tick of the window (sweep over unrolled spans, ends before starts at equal ticks).

Greedy order, mandatory (locked) events first, priority descending then index, strict `>` coverage test and ceil spacing are unchanged.

## 3. API / types

`reduceEvents(EventLane&, ActivityConfig, PermissionConfig, phraseTicks, apply)` keeps its signature. `ReduceReport::kept/dropped` become `uint16_t` (capacity 256). The function allocates
(`std::vector` of spans, message thread only, explicit user action, bounded by 256 events * 2048 repetitions); it is `noexcept`, and allocation failure terminates as for any other message-thread allocation.
Not callable from the audio thread (it never was).

## 4. Tests (`tests/core/ReduceLoopTests.cpp`; legacy cases remain in `SceneControlTests`)

| ID | Check |
|---|---|
| W3-RED-T01 | No loops: report and kept set identical to the old algorithm on a randomized corpus (reference implementation of the old greedy kept inside the test; 500 cases). |
| W3-RED-T02 | Spacing across rings: op 0 loop 3 beats event at 0, op 1 loop 4 beats event at 960 ticks; min distance is gcd-based (closest approach 0 beats at 12 beats); spacing 0.5 beat drops the lower priority one. |
| W3-RED-T03 | Self repetition: a lane with loop 1 beat and spacing 2 beats drops its event; with the Events lock it reports ConflictLocked, lane unchanged. |
| W3-RED-T04 | Coverage: a 1-beat event looping every 2 beats in a 16-beat phrase covers 50 %; cap 40 % drops it; two interleaved loops union correctly. |
| W3-RED-T05 | Peak concurrent counts operations active simultaneously on the unrolled timeline (3-beat and 4-beat lanes overlap at beat 12). |
| W3-RED-T06 | Wrap: event [3,5) beats on a 4-beat ring covers [3,4) and [0,1); phrase wrap on non-looping lane identical to legacy. |
| W3-RED-T07 | 256 events, 8 loops: finishes (functional test, no timing claim); determinism: same input twice gives identical result; apply keeps array order. |

## 5. Non-goals

Judging later phrases when P is not a multiple of a ring (reported as phrase one). Changing the live `maxConcurrent` evaluator (already ring-aware).
