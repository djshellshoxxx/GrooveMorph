# F01 — Intelligent Variation Generator

**Status:** DRAFT — interface not frozen. **Dependencies:** F08,F03. **Modules:** VariationPlanner.

## Scope and user value
Expose intelligent variation generator as an optional GrooveMorph capability driven by the existing canonical scenes, preserving manual editing and the base wet/dry workflow. Out of scope: cloud services, unbounded real-time machine learning and rewriting unrelated existing DSP.

## Requirements
- F01-FR-01: Accept validated input contract: SceneSnapshot, ConstraintSet, seed, mode, variationIndex.
- F01-FR-02: Produce VariationCandidate with stable deterministic semantics and explicit invalid-input status.
- F01-FR-03: Preserve protected beats, existing scenes and parameter IDs unless user explicitly commits an edit.
- F01-NFR-01: Callback does bounded preallocated processing with no allocation/locks/I/O; background work is cancellable.
- F01-NFR-02: Persist only accepted canonical state with forward-version rejection and migration fixtures.

## Algorithm and lifecycle proposal
Generate on worker from immutable scene snapshot; apply seeded bounded event insert/delete/shift/substitute operations using conservative/creative/experimental profiles; score edit distance, density, preserved accents and distribution; preview before atomic commit.

## Proposed public seam
```cpp
// Header/API to freeze once current repository types are reconciled.
struct F01Request; // validated input values: SceneSnapshot, ConstraintSet, seed, mode, variationIndex
struct F01Result;  // immutable VariationCandidate; explicit status and generation
// Plan on a non-real-time thread; consume validated results through bounded handoff.
F01Result planF01(const F01Request& request) noexcept(false);
```

Explicitly bind input normalization, parameter min/max/default, buffer ownership, lifetime and error codes during M1 contract review. This declaration is illustrative and must not be copied as a working implementation without completing types. Serialized revisions include schema number, source scene IDs and deterministic seed when relevant.

## User workflow and conflicts
Start from editor or performance surface, configure finite options, preview or trigger, inspect pending status, accept/undo via HistoryManager. Host automation has documented precedence over local commands. Missing capabilities are signalled without destroying existing scene edits.

## Verification and acceptance
Same input snapshot, seed and version produces byte-identical event schedule; protected events unchanged; max event count enforced; cancellation changes no persistent state.

Required tests: F01-T01 deterministic model fixture, F01-T02 malformed range/state, F01-T03 late/overflow publication, F01-T04 host tempo/seek/buffer partition, F01-T05 restore/migration, F01-T06 production-quality audio review where DSP applicable. Create real fixture inputs, expected outputs and measurement tolerances before READY approval. Instrument CPU and allocation within the exact maximum configured project and document platform-specific behavior. QA must include no-signal, impulse, drum-loop, bass-heavy, mono/stereo and automation-storm contexts where applicable.

## Integration task contract
Implement module behind isolated portable core build target first; add adapter/UI wiring as separate reviewable commit; then update parameter manifest, state schema, preset migration, docs and traceability. Gate feature merge on passing unit+integration tests, callback-safety audit, regression render and target-host checks. Do not edit shared PluginProcessor concurrently.

## Implementation-approved contract supplement (2026-10-12)

Product owner approved the proposed interface direction. This approval does not establish verified build/QA readiness. Reconcile exact binary ABI against the current source before publishing a public header. Existing `SceneBank`, `SceneSnapshot`, `VariationSettings`, `OperationType`, `Scheduler`, `TransportResolver`, APVTS stable IDs and serialized data remain authoritative; this feature shall integrate without overwriting their semantics. Primary module directory: `src/core/variation/VariationPlanner`. Dependencies: F08.

### 1. API responsibilities and ownership
Command inputs are validated, versioned plain C++ values owned by the calling editor or planner worker. Results are immutable validated plans. Audio-thread consumers get fixed-size sample/event records only, with reclamation off the callback. All UI/host adapter work is outside portable core. A rejected operation must leave the previous audio plan, scene data and host state intact. Lifecycle transitions: Idle -> Planning -> Preview -> Committed, or Idle/Planning/Preview -> Cancelled/Failed; each transition has one generation number, and stale results are rejected.

### 2. Chosen algorithm and bounded execution
Deterministic SplitMix64 over candidate index and per-operation bounded proposals. Existing SceneBank has four variation slots and five OperationSettings; version one varies depth/enable/slice beats only. Do not invent event scheduler events that are not modeled.

All finite user inputs must have explicit defaults; reject NaN/Inf, negative time, oversized arrays, unknown enum values, incompatible future state, or a missing referenced scene. Use deterministic event tie-breaking and document unavoidable signal-processing approximation. Maximum event count, queue capacity and CPU budget must be measured and frozen in the shared benchmark specification before a release gate passes.

### 3. Real-time and platform contract
Allocate and preprocess on UI/background thread or in prepareToPlay. No allocation, destruction of owning snapshot storage, mutex waits, file access, dynamic string formatting, logging, or UI calls in processBlock. Host adapter uses sample offsets clamped to current buffer with valid transport epochs; absent PPQ falls back to the documented manual tempo origin. Explicitly verify Windows MSVC and Linux GCC/Clang numerical behavior, standalone and VST3 lifecycle, state restore and CLAP capability when adapter is available.

### 4. User-facing workflow and accessibility
Quick mode exposes safe presets/preview, Editor mode exposes validated controls and revision management, Performance mode exposes only time-critical actions. Each control must have discoverable label and current value, keyboard operation, status feedback, appropriate automation and persisted settings. Preview never modifies the stored scene; commit is one undoable edit. Missing host capabilities are explained and do not silently alter scene state.

### 5. Verification plan and acceptance
Pilot: same seed and candidate index yields exactly identical settings; protection mask leaves matching operations unchanged; valid outputs obey depth [0,1], slice [1/16,4].

Test F01-T01: fixed seed/input produces exact repeated output; F01-T02: malformed or unknown fields are rejected without state mutation; F01-T03: fixed-capacity queue saturation and stale generations preserve safe output; F01-T04: buffer sizes 1, 32, 256, 2048 and 44.1/48/96 kHz with tempo/seek changes; F01-T05: host project save/reopen and schema migration; F01-T06: audio rendering and latency/callback profiling when this feature touches sound; F01-T07: interface-to-application wiring and controller/automation behavior. Each fixture must retain expected values, target platform, command, commit SHA and pass/fail result. No feature is VERIFIED_COMPLETE on documentation alone.

### 6. Failure recovery, documentation and merge gate
On invalid edit, restore previous immutable version. On late planner publication, use last valid schedule. On overflow, increment a fixed-size status counter and apply documented priority. On missing dependency, visibly disable only the dependent function. Maintain user documentation, API contract and migration notes. Isolate changes by module, require targeted and cross-feature test results, and merge against refreshed integration head. Shared PluginProcessor, PluginEditor, state schema and CMake changes have one owner.
