# W3-CAPACITY: 256 events per lane and 8 protected regions

Status: SPEC (2026-10-20, base `c83e8b2`). Closes the capacity deviation of C01/C03 (spec: 256 events, 8 regions).

## 1. Values

| Constant | Before | After |
|---|---|---|
| `control::maxLaneEvents` | 32 | 256 |
| `control::maxProtectedRegions` | 4 | 8 |
| `EventLane::count` | `uint8_t` | `uint16_t` |
| `ReduceReport::kept/dropped`, `ScaleReport::scaled/dropped/skippedLocked` | `uint8_t` | `uint16_t` |

Still fixed-capacity arrays, no allocation. 32-bit event-index bit masks in Reduce are replaced by fixed arrays.

## 2. Real-time size check

`LaneEvent` is 28 bytes: lane 7168 B, `SceneControl` about 7.4 KB, `VariationSettings` about 7.5 KB, `SceneSnapshot` (4 scenes) about 30 KB. The audio thread copies a snapshot only when a new one is published
(`pullScene`, SpinLock try-lock, previous copy kept on contention): one 30 KB `memcpy` (microseconds) per scene edit, not per block. `static_assert`s and a test fix `is_trivially_copyable<SceneSnapshot>` and
`sizeof(SceneSnapshot) <= 64 KiB`. Per render segment the processor no longer copies the 7.5 KB settings: `effectiveSettings` returns a reference (snapshot or the morph cache) and the gated engine input is a
operationCount-element operations array. `evaluate()` is O(events) per segment; worst case (256 events, edges every sample) is bounded by block length * 256 iterations. No timing is claimed (not measured).

## 3. Serialization

Keys unchanged (`variation.<i>.event.<k>.*`, `k` < 256; `region.<r>.*`, `r` < 8). The parser's per-scene pending arrays grow accordingly (about 30 KB on the parsing stack/heap object, message thread).
Full-load size: 4 scenes * 256 events * about 330 bytes is under 350 KB of scene text, about 400 KB after XML escaping, inside the 1 MiB state limit (`maximumStateBytes`); a test fixes the figure.
Files written by older builds (<= 32 events, <= 4 regions) load unchanged; files with more than the capacity are clamped with a recovery note (no crash, no partial event).

## 4. UI strings (minimal edits)

"Event limit reached (32)." uses the constant; the region combo lists 8 regions; the Info text says "up to 8 protected regions" and "256 events". No layout change.

## 5. Acceptance tests (`tests/core/CapacityTests.cpp`)

| ID | Check |
|---|---|
| W3-CAP-T01 | 256 events can be added, the 257th returns `Capacity`; remove/replace/overlap on a full lane. |
| W3-CAP-T02 | Full bank (4 x 256 events, 8 regions each) round trips exactly; serialized size < 400 KB; parse time is not asserted. |
| W3-CAP-T03 | Legacy fixture: a v1 file with 32 events and 4 regions (written by the alpha format) loads identically; file with event index 300 or region 9 is clamped with a note. |
| W3-CAP-T04 | `evaluate()` with 256 events: correct active set at 1000 random positions vs a brute-force reference; `ticksUntilChange` never skips a boundary. |
| W3-CAP-T05 | `reduceEvents`, `scaleEvents`, `enforcePermissions`, `buildCandidate` on full lanes: no overflow, counts <= 256, locked events kept. |
| W3-CAP-T06 | `SceneSnapshot` trivially copyable and <= 64 KiB; `PublishedScene` copy compiles under `noexcept`. |
| W3-CAP-T07 | 8 regions mute the evaluator inside every region; region 9 is rejected by the serializer clamp. |

## 6. Non-goals

Dynamic capacity, per-event names, delta-compressed scene text (the 1 MiB limit stays).
