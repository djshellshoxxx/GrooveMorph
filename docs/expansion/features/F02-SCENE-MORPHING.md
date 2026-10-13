# F02 — Scene Morphing Engine

**Status:** DRAFT — interface not frozen. **Dependencies:** F08,F06. **Modules:** MorphEngine,TransitionMixer.

## Scope and user value
Expose scene morphing engine as an optional GrooveMorph capability driven by the existing canonical scenes, preserving manual editing and the base wet/dry workflow. Out of scope: cloud services, unbounded real-time machine learning and rewriting unrelated existing DSP.

## Requirements
- F02-FR-01: Accept validated input contract: from/to scene handles, correspondence map, progress, curve.
- F02-FR-02: Produce MorphSchedule with stable deterministic semantics and explicit invalid-input status.
- F02-FR-03: Preserve protected beats, existing scenes and parameter IDs unless user explicitly commits an edit.
- F02-NFR-01: Callback does bounded preallocated processing with no allocation/locks/I/O; background work is cancellable.
- F02-NFR-02: Persist only accepted canonical state with forward-version rejection and migration fixtures.

## Algorithm and lifecycle proposal
Match events by lane, rhythmic phase, operation class and weighted time distance; precompute minimum-cost correspondence with bounded candidate pool; interpolate continuous fields only when semantics match, crossfade unmatched or discrete operations at safe boundaries; preserve phase and delay alignment.

## Proposed public seam
```cpp
// Header/API to freeze once current repository types are reconciled.
struct F02Request; // validated input values: from/to scene handles, correspondence map, progress, curve
struct F02Result;  // immutable MorphSchedule; explicit status and generation
// Plan on a non-real-time thread; consume validated results through bounded handoff.
F02Result planF02(const F02Request& request) noexcept(false);
```

Explicitly bind input normalization, parameter min/max/default, buffer ownership, lifetime and error codes during M1 contract review. This declaration is illustrative and must not be copied as a working implementation without completing types. Serialized revisions include schema number, source scene IDs and deterministic seed when relevant.

## User workflow and conflicts
Start from editor or performance surface, configure finite options, preview or trigger, inspect pending status, accept/undo via HistoryManager. Host automation has documented precedence over local commands. Missing capabilities are signalled without destroying existing scene edits.

## Verification and acceptance
Deterministic samples at progress 0 and 1 equal target endpoints; no allocations in callback; discontinuity measurement over impulses, drums and rapid reversal automation.

Required tests: F02-T01 deterministic model fixture, F02-T02 malformed range/state, F02-T03 late/overflow publication, F02-T04 host tempo/seek/buffer partition, F02-T05 restore/migration, F02-T06 production-quality audio review where DSP applicable. Create real fixture inputs, expected outputs and measurement tolerances before READY approval. Instrument CPU and allocation within the exact maximum configured project and document platform-specific behavior. QA must include no-signal, impulse, drum-loop, bass-heavy, mono/stereo and automation-storm contexts where applicable.

## Integration task contract
Implement module behind isolated portable core build target first; add adapter/UI wiring as separate reviewable commit; then update parameter manifest, state schema, preset migration, docs and traceability. Gate feature merge on passing unit+integration tests, callback-safety audit, regression render and target-host checks. Do not edit shared PluginProcessor concurrently.

## Implementation-approved contract supplement (2026-10-12)

Product owner approved the proposed interface direction. This approval does not establish verified build/QA readiness. Reconcile exact binary ABI against the current source before publishing a public header. Existing `SceneBank`, `SceneSnapshot`, `VariationSettings`, `OperationType`, `Scheduler`, `TransportResolver`, APVTS stable IDs and serialized data remain authoritative; this feature shall integrate without overwriting their semantics. Primary module directory: `src/core/morph/MorphPlanner`. Dependencies: F01,F08.

### 1. API responsibilities and ownership
Command inputs are validated, versioned plain C++ values owned by the calling editor or planner worker. Results are immutable validated plans. Audio-thread consumers get fixed-size sample/event records only, with reclamation off the callback. All UI/host adapter work is outside portable core. A rejected operation must leave the previous audio plan, scene data and host state intact. Lifecycle transitions: Idle -> Planning -> Preview -> Committed, or Idle/Planning/Preview -> Cancelled/Failed; each transition has one generation number, and stale results are rejected.

### 2. Chosen algorithm and bounded execution
Normalize scene operation pairs by fixed OperationType index. Continuous depths interpolate with t in [0,1]; slice subdivision is discrete selected on quantized boundary; enable toggle switches only at a protected boundary. Render two complete audio streams and equal-power blend only when CPU budget permits; otherwise controlled dry fallback. Quantize automation targets outside callback.

All finite user inputs must have explicit defaults; reject NaN/Inf, negative time, oversized arrays, unknown enum values, incompatible future state, or a missing referenced scene. Use deterministic event tie-breaking and document unavoidable signal-processing approximation. Maximum event count, queue capacity and CPU budget must be measured and frozen in the shared benchmark specification before a release gate passes.

### 3. Real-time and platform contract
Allocate and preprocess on UI/background thread or in prepareToPlay. No allocation, destruction of owning snapshot storage, mutex waits, file access, dynamic string formatting, logging, or UI calls in processBlock. Host adapter uses sample offsets clamped to current buffer with valid transport epochs; absent PPQ falls back to the documented manual tempo origin. Explicitly verify Windows MSVC and Linux GCC/Clang numerical behavior, standalone and VST3 lifecycle, state restore and CLAP capability when adapter is available.

### 4. User-facing workflow and accessibility
Quick mode exposes safe presets/preview, Editor mode exposes validated controls and revision management, Performance mode exposes only time-critical actions. Each control must have discoverable label and current value, keyboard operation, status feedback, appropriate automation and persisted settings. Preview never modifies the stored scene; commit is one undoable edit. Missing host capabilities are explained and do not silently alter scene state.

### 5. Verification plan and acceptance
Endpoints t=0 and t=1 reproduce source and destination; no sudden shift of slice offset without fade; continuous parameter sweeps deterministic.

Test F02-T01: fixed seed/input produces exact repeated output; F02-T02: malformed or unknown fields are rejected without state mutation; F02-T03: fixed-capacity queue saturation and stale generations preserve safe output; F02-T04: buffer sizes 1, 32, 256, 2048 and 44.1/48/96 kHz with tempo/seek changes; F02-T05: host project save/reopen and schema migration; F02-T06: audio rendering and latency/callback profiling when this feature touches sound; F02-T07: interface-to-application wiring and controller/automation behavior. Each fixture must retain expected values, target platform, command, commit SHA and pass/fail result. No feature is VERIFIED_COMPLETE on documentation alone.

### 6. Failure recovery, documentation and merge gate
On invalid edit, restore previous immutable version. On late planner publication, use last valid schedule. On overflow, increment a fixed-size status counter and apply documented priority. On missing dependency, visibly disable only the dependent function. Maintain user documentation, API contract and migration notes. Isolate changes by module, require targeted and cross-feature test results, and merge against refreshed integration head. Shared PluginProcessor, PluginEditor, state schema and CMake changes have one owner.
