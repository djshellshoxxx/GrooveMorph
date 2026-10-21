# F06 Polyrhythmic and independent operation lanes

Status: IMPLEMENTED (per-lane loop length), spec COMPLETE. Written 2026-10-20 against `c83e8b2`.
Replaces draft `docs/expansion/features/F06-POLYRHYTHMIC-LANES.md` (PR #6). The draft's rational lane periods and lookahead scheduler are
NOT used: the shipped design is a loop ring per operation inside the placement evaluator (DECISIONS: "Polyrhythm = per-lane loop rings").

## 1. Objective

Let each of the five operation lanes repeat its events on its own length in meter beats (for example 3 beats against 4) so lanes
drift against each other and against the phrase, with no second clock or scheduler.

## 2. As implemented

- Data: `EventLane::loopBeats[5]` (`uint8`, meter beats, 0 = follow the phrase, sanitised to 0..64) in `src/core/control/SceneControl.h`.
- Evaluation (`PlacementEvaluator.cpp`, `evaluate()`): for an event of operation `op` with `loop > 0`, `ring = loop * 960` ticks,
  `opPhase = positiveMod(q, ring)` where `q` is the pattern position minus `originTicks`; the event matches when
  `positiveMod(opPhase - fmod(onset, ring), ring) < min(duration, ring)`. `ticksUntilChange` takes the event start, event end and the ring
  wrap for every looping event, so edges are sample-accurate at any block size (renderer splits at `ticksUntilChange`).
- Overlap checks while editing use the lane's own ring (`eventsOverlap` with `ring`; `addEvent`/`replaceEvent` in `SceneControl.cpp`
  use `loopBeats[op % laneCount]`). Reduce/Analyze still use the PHRASE ring (loop-aware Reduce is owned by W3).
- `restartCycle` anchors use `pos - since` on the lane ring, so a restart event restarts at each loop repetition.
- C03: `enforcePermissions` restores `loopBeats[op]` for operations whose events are protected (locked event or Events lock).
- Generator keeps `loopBeats` unchanged (`generateEventPatterns` copies them).

## 3. Parameters

Scene data, not host parameters. Range 0..64 beats per lane, default 0 for all five.

## 4. State keys

`variation.<i>.lane.loop.<op>` for op 0..4 (written only for non-default scenes; ignored by older builds; format stays `groovemorph-scenes v1`).
Missing -> 0.

## 5. Real-time and threading

Pure arithmetic over at most `maxLaneEvents` (32; 256 after W3) events in the evaluator; no allocation or locks. The evaluator is called once per
render segment, so the per-block cost is O(events) per segment; with 256 events and frequent boundaries W3 must keep the loop branch
free of division where possible and report the worst case in its test notes (do not claim a measured figure without running it).

## 6. UI wiring

Place tab: a lane combo (`loopLane`) plus a 0..64 slider (`loopBeats`; 0 displays as "phrase", otherwise "N beats"); the tooltip explains that a
3-beat and 4-beat lane drift. The canvas draws events wrapped on the PHRASE, not on the lane ring.

## 7. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F06-T01 | Looped event fires every 3 beats, silent between, boundary distance exact | EXISTS | `tests/core/SceneControlTests.cpp` "add looped event", "loop fires every 3 beats", "loop silent between", "loop boundary distance" |
| F06-T02 | Overlap is judged on the lane's own loop ring; same event fine without loop | EXISTS | SceneControlTests "overlap is judged on the lane's own loop ring", "without a loop the same event is fine" |
| F06-T03 | Loop lengths survive generation and permissions | EXISTS | CandidateBuilderTests "lane loop lengths retained"; SceneControlTests "candidate loop lengths kept" |
| F06-T04 | Loop lengths round-trip in the scene format; value > 64 is clamped | PARTIAL: round trip of all five lane lengths EXISTS (SceneControlTests "all five operations round-trip" fixture sets `loopBeats[op]=2+op`); clamp to 64 MISSING | SceneControlTests / SceneSerializerTests |
| F06-T05 | Meter change and seek re-anchor: ring phase follows the pattern position; a negative position wraps | MISSING | add SceneControlTests case with `positionBeats < 0` and loop > 0 |
| F06-T06 | Looping lane with `restartCycle` restarts the engine grid at each repetition | MISSING | EngineGoldenTests-style render |
| F06-T07 | Audio test: two looping lanes with 3 and 4 beats render sample-accurate edges at block sizes 1, 32, 256, 2048 | MISSING | processor render test |
| F06-T08 | Canvas draws the loop ring tiles | MISSING (feature absent; native UI) | manual QA |
| F06-T09 | Loop-aware Reduce keeps spacing/coverage on each lane's ring | MISSING, owned by W3 | W3 tests |

## 8. Gaps

- W3: loop-aware Reduce (spacing and coverage measured on each lane's ring), capacity 256.
- Unowned, deferred: drawing the loop ring in the Place canvas; per-lane tempo ratios (non-integer periods); an LCM view.

## 9. Non-goals

Rational or fractional lane periods, a second clock, and event generation that crosses lanes (W6 Euclidean patterns use the same loop
rings but are specified in [N03](N03-EUCLID-CHANCE.md)).
