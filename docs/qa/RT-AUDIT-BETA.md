# Beta audit A3: real-time safety, concurrency, sanitizers, hostile state

Auditor A3, 2026-10-20. Base: `claude/beta-integration` at `488dd77` (29 core tests at the workplan baseline, 50 before this
audit). Scope: the portable core (`src/core/`) and, by reading only, the JUCE processor files
(`src/PluginProcessor.cpp`, `PluginProcessorEasy.cpp`, `PluginProcessorLoop.cpp`).

## 1. What was and was not verified

| Check | Result | Evidence |
|---|---|---|
| Whole core suite under ASan + UBSan | 53/53 pass | `build-asan`, `-fsanitize=address,undefined -fno-sanitize-recover=undefined`, Release `-O1 -g`, GCC 13.3, `detect_leaks=1` |
| Whole core suite under TSan | 53/53 pass | `build-tsan`, `-fsanitize=thread`, same optimisation |
| Whole core suite under UBSan + `float-divide-by-zero` | 53/53 pass | `build-ubfp` |
| Hostile-state fuzz, deeper than the CI default | 5 seeds x 4000-6000 inputs per target, clean after the fixes below | `GM_FUZZ_ITERS`, `GM_FUZZ_SEED` |
| `-fsanitize=float-cast-overflow` | **Not effective** | GCC 13.3 compiles the flag but never reports (shown with a 6-line `1e25 -> int64` program). Clang 18 is installed but its sanitizer runtime is not, so it cannot link. Out-of-range float-to-int casts were therefore found by reading, not by tooling (A3-04, A3-05, A3-06). |
| JUCE processor files (`PluginProcessor*.cpp`) | **Read only, not compiled, not run** | This machine has no JUCE system dependencies. Every statement about them below is from code reading. No processor file was changed. |
| Real hosts, MSVC, ARM/Apple clang, MSan, Valgrind | Not run | Unavailable here |
| Timing numbers below | Single run on a shared 2-core VM, GCC `-O2`, no repetition statistics | Treat as orders of magnitude |

Commands used (from the worktree root):

```
cmake -S . -B build-asan -G Ninja -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g" "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"
cmake -S . -B build-tsan ... "-DCMAKE_CXX_FLAGS=-fsanitize=thread -fno-omit-frame-pointer" "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread"
cmake --build build-asan -j2 && ctest --test-dir build-asan --output-on-failure -j2
GM_FUZZ_ITERS=4000 GM_FUZZ_SEED=3 ASAN_OPTIONS=max_allocation_size_mb=300 build-asan/HostileStateFuzzTests
```

No existing test was weakened or edited.

## 2. Findings

Severity: **High** = crash, hang or out-of-memory reachable from a state file or host input. **Low** = undefined behaviour
or wrong state only reachable with values the rest of the system already bounds (defence in depth).

| ID | Sev | Where | Defect | Fix | Test that catches it |
|---|---|---|---|---|---|
| A3-03 | High | `reduceEvents` (`SceneControl.cpp`) | `unroll()` expanded every event over the whole phrase. With a short loop ring (`loopBeats = 1`, 960 ticks) and a phrase length of `2^40` or `INT64_MAX` it needed billions of `Span`s: the process was killed by the OOM killer (observed during fuzzing). It is `noexcept` and uses `std::vector`, so even a clean `bad_alloc` would have been `std::terminate`. Real phrases are at most 64 bars x 32 beats = 1,966,080 ticks and are safe (about 0.2 s and 8 MB at that limit, measured). | New `maxPhraseTicks` (SceneControl.h); `reduceEvents` returns an unchanged default report outside `1..maxPhraseTicks`. | `HostileStateFuzzTests::regressionReduceHugePhrase`, plus the edit-command fuzzer |
| A3-07 | High | `applyEuclid` (`EuclidPattern.cpp`) | `tiles = (ring + tileTicks - 1) / tileTicks` overflowed signed arithmetic for a huge phrase (UBSan: `7680 + 9223372036854775807`) and, for phrases around `2^40`, loops over about 10^10 tiles on the message thread. | Reject `phraseTicks > maxPhraseTicks` as `Invalid`. | `regressionEuclidHugePhrase` |
| A3-05 | Low | `LoopCapture::process` (`LoopSource.cpp`) | `std::llround(beats * samplesPerBeat)` is undefined for huge or tiny products. On x86 it yields `LLONG_MIN`, which passed the `wanted > capacity` test and reached `Done` with a negative recorded length. Unreachable today because `PhraseClock` limits tempo to 20..300 BPM and the rate to 8..384 kHz, but the class is public. | Reject `!(wanted >= 1)` or non-finite as failure 1 before converting; compare in double against the capacity. | `hostileDspArguments` (verified to fail on the old code: "loop capture length sane") |
| A3-04 | Low | `evaluate` (`PlacementEvaluator.cpp`) | `static_cast<int64_t>(floor((q - onset) / ring))` for the chance cycle index overflows for positions above about `9e15` beats. Upstream bounds the transport position far below that. | `floorToInt64()` saturates (also for NaN). Result unchanged for every in-range value. | `hostileDspArguments` (documents the contract; cannot detect the cast with GCC) |
| A3-06 | Low | `sliceCursor` (`SweepCrush.h`) | Same cast for the FilterSweep/BitCrush slice index (`beat / sliceBeats`, up to 16x larger). Only the index parity is used. | Saturate to 0 outside `+-4e18`, NaN included. | `hostileDspArguments` (non-finite phase offsets) |
| A3-08 | Low | `RhythmEngine::readRing` | A NaN read position would pass `std::clamp` unchanged and reach `floor` and an `int64` cast. Only reachable with a NaN `phaseOffsetBeats`, which the evaluator never produces. | `!isfinite(r)` falls back to "now" (only on a path that was already undefined). Golden tests unchanged. | `hostileDspArguments` |

No sanitizer report came from the pre-existing suite, from the new counting-allocator test, from the TSan stress tests, or
from the fuzzers other than A3-03 and A3-07.

### Reviewed and found sound (no change)

**Scene hand-off size (the brief's main question).** Measured with a 6-line program on this VM:

| Type | `sizeof` |
|---|---|
| `LaneEvent` | 24 B |
| `EventLane` (256 events, 7 loop lengths, 7 chances, seed) | 6,168 B |
| `VariationSettings` | 6,388 B |
| `SceneSnapshot` (4 scenes + MIDI map) | 25,592 B |
| `PublishedScene` (snapshot + evolution plan holding one more `VariationSettings`) | about 32 KB |

The copy is a plain `memcpy` (the type is trivially copyable; a `static_assert` now enforces that): 0.96 us hot-cache for the
snapshot, about 1 to 3 us for `PublishedScene`. `pullScene()` takes a `juce::SpinLock` with **try-lock only** and does the copy
only when the version counter changed, never per block. The message thread holds the same spin lock for the same short copy.
If it loses the race the audio thread keeps the previous snapshot and retries next block (bounded, never blocks). A torn
read is impossible while the copy is inside the lock; a stale version number only causes one extra copy. **Decision: no
redesign.** A pointer-swap scheme would add a reclamation problem for no measurable gain. `RtAllocationTests::testSnapshotBudget`
fails if the snapshot stops being trivially copyable or grows past 64 KiB, which is the point at which this decision should be
revisited.

**Per-segment CPU.** `renderSegment` runs `effectiveSettings()` (trims, Chaos), `evaluate()` and a 6.4 KB `gated` copy once per
segment, and segments are cut at every placement boundary. Measured, GCC `-O2`, 252-event lane, all operations on:

| Operation | Cost |
|---|---|
| `evaluate()` | 11 us |
| `applyChaos()` | 9 us |
| 6.4 KB settings copy | 0.003 us |
| `morph()` on a cache miss (O(n^2) event matching) | 177 us |
| `RhythmEngine::process`, 512 frames stereo, all 7 operations, tracing off | 184 us per block (1.7 % of a 48 kHz block) |
| Hostile lane: 7 x 36 one-tick events, 300 BPM, Chaos on, per host block of 512 | mean 45 us, worst 1.0 ms with 52 segments (10 % of the 10.7 ms budget) |
| Same, 64-frame blocks | mean 19 us, worst 157 us with 8 segments (12 % of 1.33 ms) |

The segment count per block is bounded by the number of distinct boundaries inside the block, and one tick is at least 9 samples
at 44.1 kHz and 300 BPM, so the worst case is about 56 segments per 512 frames. No overrun was produced. Residual: cache the
Chaos overlay across segments (it is recomputed from identical inputs) if profiling on real hosts ever shows a problem. Not done,
because it would change `PluginProcessor.cpp`, which could not be compiled here.

**Morph automation.** `morph()` is rebuilt only when its key changes; automating the Morph amount changes the key every block,
so each block pays up to the 177 us above (1.7 % of a 512-frame block, 13 % of a 64-frame block). Acceptable; recorded.

**Audio-thread call graph, component by component** (reading; the new allocation test covers the portable parts at run time):

| Component | Allocation / lock / I/O | Notes |
|---|---|---|
| `PluginProcessor::process` | none. `std::vector` appears only in `applyMidiLinks()`, on the message-thread timer | `ScopedNoDenormals`; whole buffer sanitized before any early return; channels beyond 2 cleared; `maxBlock` chunking guards hosts that deliver larger blocks; every cross-thread value is an atomic or handed over by the try-lock |
| `pullScene`, `evolutionBoundary`, `morphedSettings`, `effectiveSettings` | none | see above; `audioScene`/`morphCache`/`effectiveScratch` are audio-thread only (only `prepareToPlay` touches them, with the stream stopped) |
| `RhythmEngine` | none after `prepare()` (4 rings x channels, sweep/crush state sized there) | bounded loop per sample, at most 2 fade taps; non-finite input replaced; output `x == dry` shortcut; sub-1e-30 flushed; trace push is wait-free |
| `PlacementEvaluator` | none | loops bounded by `maxLaneEvents` (256) and 8 regions; insertion sort of at most 7 items |
| `SceneControl` overlays, `OperationTrims::applyTrims`, `VariationBlend::morph`, `ChaosMapper::applyChaos` | none; value types on the stack | `applyChaos` makes 3 copies of a 6 KB struct per call (9 us) |
| `Chance.h` | none, stateless hash | |
| `Crossover` (`BandSplit`) | none after `prepare()` (vectors sized there; `split`/`merge` clamp `n` to the prepared size) | double-precision states reset when non-finite; mono and stereo only |
| `OnsetDetector` | none | subnormal flush |
| `GestureLane` | message thread only (`serialize`, `deserialize` use strings); `sample()` is allocation free | |
| `MidiLinks` (`CcInbox`) | none: relaxed atomics, single writer; `push` is O(1), `collect` runs on the message thread | A relaxed travel/value pair can be observed one tick apart; the effect is a one-tick delay of a report |
| `OutputCapture` | none on the audio path: buffers allocated in `arm()` (message thread), freed in `release()` after a bounded wait for `busy` | Dekker-style `busy`/`state` handshake with seq_cst; tested under TSan |
| `LoopSource` | none: double buffer plus `reading` flag; `load()` waits (bounded, up to 100 ms of `sleep_for`) on the **message thread** | tested under TSan; removing the wait is detected as a data race (mutation check done, then reverted) |
| `LoopCapture` | none | A3-05 |
| `TimeStretcher` / `StretchWorker` | not on the audio thread; results reach the audio thread only through `LoopSource::load` | storm test added |
| `TraceRing` | none: seqlock with relaxed atomics and fences. TSan does not model fences, so its correctness is by review (writer odd/fence/words/even; reader acquire/words/fence/recheck) and by the existing `TraceRingTests` | |
| `Scheduler`, `TransportResolver`, `PhraseClock`, `AutoReturn`, `LaunchPhase`, `PhaseResolver`, `MidiNoteMap` triggers | none | hostile host values rejected or clamped (ppq <= 1e9, tempo 20..300, rate 8k..384k); wide arithmetic for sample positions |
| Easy-mode and Loop processor files | message thread only | `requestSurprise`, `updateSurprise`, `updateLoopStretch` take `modelMutex` or `loopServiceMutex`, never from `process()` |

**Denormals and non-finite values.** `ScopedNoDenormals` covers the callback on x86 and ARM; the engine, SVF, onset detector and
crossover additionally flush tiny state. Non-finite samples are replaced by exactly 0 at three places (start of `process`, inside
the engine/crossover/captures, and the final guard in `processBlock`). Tested with NaN/Inf injected into the band split and with non-finite
phase offsets, crossfade and tempo values given to the engine and the captures (outputs stay finite).

**Atomics.** No atomic is used as a lock. Counters and status values are relaxed single-writer; handshakes (`busy`/`state`,
`reading`/`active`, `sceneVersion`) use seq_cst or release/acquire as needed. No misuse found.

## 3. Tests added

All are auto-registered by the existing CMake glob (`tests/core/*Tests.cpp`) and pass in the normal Release build and under
all three sanitizer builds.

| File | What it proves |
|---|---|
| `tests/core/RtAllocationTests.cpp` | Replaces global `operator new`/`delete`. Asserts **zero allocations and zero deallocations** (thread-local arming) during: `RhythmEngine::process` for all 7 operations with a 252-event lane, chance, loops, regions, activity limit, tracing on, 11 timing cases including NaN, Inf, huge and negative; `evaluate`; `applyChaos`; `morph`/`blendDistance`; `applyTrims`; `enforcePermissions`; phase resolution; `eventFires` (Euclid chance); `BandSplit` LR2/LR4; `OutputCapture`; `LoopSource::render`; `LoopCapture`; `OnsetDetector`; `DemoSource`; `extractWet`; `sanitizeBlock`; `Scheduler`; `TransportResolver`; `PhraseClock`; `AutoReturn`; MIDI trigger; `CcInbox`; scene snapshot copy. A self-check proves the hook counts. |
| `tests/core/ConcurrencyHandoffTests.cpp` | Two-thread stress of `LoopSource` (no torn block, no foreign buffer), `OutputCapture` (arm/release against a running callback, recorded body is a gapless ramp), `LoopCapture`, `StretchWorker` (submit/cancel/take storm and shutdown). Meaningful under TSan. |
| `tests/core/HostileStateFuzzTests.cpp` | Deterministic mutation fuzzing: scene text (flips, deletions, duplication, truncation at every offset, NUL bytes, 60 KB lines, repeated lines, event and region keys with indices and values such as `99999999999999999999`, `1e999`, `nan`, `-2147483649`), `.gmpreset` files (factory and user), loop blobs (also re-signed after mutation so the fields and the Rice decoder are reached; forged headers claiming 20 s with a tiny payload), gesture and MIDI-link text, `mapLoop`, lane edit commands with `INT32`/`INT64` extremes, Euclid, Reduce, Scale. Checks: no crash or sanitizer report; recovered banks are already sanitized and round-trip exactly; recovery notes bounded; name length and charset; preset and blob invariants; parse time under 5 s (slowest seen: 19 ms); single allocation under 64 MiB (largest seen: 6 MB for text, 4 MB for presets, 0.1 MB for blobs); recovered scenes are fed to the engine and stay finite. Regression tests pin A3-03 and A3-07 (huge phrase lengths) and A3-04..A3-08 (`hostileDspArguments`). |

The decode of a *valid* maximum loop blob (20 s at 384 kHz, stereo) can transiently hold about 120 MB (two 30 MB sample
vectors plus the two float outputs). It is bounded by the 10 MiB encoded limit and the sample-count check made before any
allocation, runs on the message thread, and is recorded here as accepted.

## 4. Decisions

1. **Keep the `SpinLock` try-lock + trivially-copyable snapshot hand-off.** 25.6 KB (32 KB with the evolution plan) copied in
   about 1 to 3 us only on change; pointer-swap not needed. Guard test added (section 2).
2. **Add `maxPhraseTicks` (1,966,080) as the documented upper bound for message-thread algorithms that scale with the phrase.**
   Public behaviour inside the editor's range is unchanged; larger inputs were never reachable from the UI.
3. **No change to `PluginProcessor*.cpp`.** Every defect found was in the portable core. The two processor-level improvements
   noted (cache the Chaos overlay across segments; cap segments per block) are not justified by the measurements above and
   could not be compiled or run here.
4. **Message-thread blocking calls kept.** `LoopSource::load` and `OutputCapture::release` may sleep up to 100 ms on the message
   thread when the audio thread is stuck. They never run on the audio thread.

## 5. Open items for the integrator

- Run the three sanitizer configurations (and ideally Clang with `-fsanitize=float-cast-overflow`) in CI where the toolchain
  allows; the new tests are deterministic and take under 2 minutes in total under ASan on a 2-core VM.
- The JUCE processor files remain unverified at run time. A host-level soak (long run with automation of Morph, Chaos, scene
  launches and transport seeks) is not covered by anything in this audit.
