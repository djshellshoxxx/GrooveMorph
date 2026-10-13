# F03 — Smart Groove Protection

**Status:** DRAFT — interface not frozen. **Dependencies:** F06,F10. **Modules:** ConstraintEngine,GrooveProtectionEngine.

## Scope and user value
Expose smart groove protection as an optional GrooveMorph capability driven by the existing canonical scenes, preserving manual editing and the base wet/dry workflow. Out of scope: cloud services, unbounded real-time machine learning and rewriting unrelated existing DSP.

## Requirements
- F03-FR-01: Accept validated input contract: manual beat masks, band masks, detection proposals.
- F03-FR-02: Produce ProtectionMask with stable deterministic semantics and explicit invalid-input status.
- F03-FR-03: Preserve protected beats, existing scenes and parameter IDs unless user explicitly commits an edit.
- F03-NFR-01: Callback does bounded preallocated processing with no allocation/locks/I/O; background work is cancellable.
- F03-NFR-02: Persist only accepted canonical state with forward-version rejection and migration fixtures.

## Algorithm and lifecycle proposal
Manually specified immutable exclusion masks dominate any inferred transients; protect intervals in musical time and optional frequency bands; validate masks before publishing; use phase-aligned dry routing for protected audio.

## Proposed public seam
```cpp
// Header/API to freeze once current repository types are reconciled.
struct F03Request; // validated input values: manual beat masks, band masks, detection proposals
struct F03Result;  // immutable ProtectionMask; explicit status and generation
// Plan on a non-real-time thread; consume validated results through bounded handoff.
F03Result planF03(const F03Request& request) noexcept(false);
```

Explicitly bind input normalization, parameter min/max/default, buffer ownership, lifetime and error codes during M1 contract review. This declaration is illustrative and must not be copied as a working implementation without completing types. Serialized revisions include schema number, source scene IDs and deterministic seed when relevant.

## User workflow and conflicts
Start from editor or performance surface, configure finite options, preview or trigger, inspect pending status, accept/undo via HistoryManager. Host automation has documented precedence over local commands. Missing capabilities are signalled without destroying existing scene edits.

## Verification and acceptance
Generated events never modify protected intervals; signal-difference tests on dry-protected regions; crossover reconstruction and phase measurements before any automatic band protection.

Required tests: F03-T01 deterministic model fixture, F03-T02 malformed range/state, F03-T03 late/overflow publication, F03-T04 host tempo/seek/buffer partition, F03-T05 restore/migration, F03-T06 production-quality audio review where DSP applicable. Create real fixture inputs, expected outputs and measurement tolerances before READY approval. Instrument CPU and allocation within the exact maximum configured project and document platform-specific behavior. QA must include no-signal, impulse, drum-loop, bass-heavy, mono/stereo and automation-storm contexts where applicable.

## Integration task contract
Implement module behind isolated portable core build target first; add adapter/UI wiring as separate reviewable commit; then update parameter manifest, state schema, preset migration, docs and traceability. Gate feature merge on passing unit+integration tests, callback-safety audit, regression render and target-host checks. Do not edit shared PluginProcessor concurrently.

## Implementation-approved contract supplement (2026-10-12)

Product owner approved the proposed interface direction. This approval does not establish verified build/QA readiness. Reconcile exact binary ABI against the current source before publishing a public header. Existing `SceneBank`, `SceneSnapshot`, `VariationSettings`, `OperationType`, `Scheduler`, `TransportResolver`, APVTS stable IDs and serialized data remain authoritative; this feature shall integrate without overwriting their semantics. Primary module directory: `src/core/protect/ProtectionMask`. Dependencies: F06.

### 1. API responsibilities and ownership
Command inputs are validated, versioned plain C++ values owned by the calling editor or planner worker. Results are immutable validated plans. Audio-thread consumers get fixed-size sample/event records only, with reclamation off the callback. All UI/host adapter work is outside portable core. A rejected operation must leave the previous audio plan, scene data and host state intact. Lifecycle transitions: Idle -> Planning -> Preview -> Committed, or Idle/Planning/Preview -> Cancelled/Failed; each transition has one generation number, and stale results are rejected.

### 2. Chosen algorithm and bounded execution
Map manually protected musical intervals to half-open beat ranges [start,end). Evaluate mask per scheduled event; if overlap then reject or split transformation only when phase-aligned dry mixing is proven. Automatic transient proposals never overwrite manual regions.

All finite user inputs must have explicit defaults; reject NaN/Inf, negative time, oversized arrays, unknown enum values, incompatible future state, or a missing referenced scene. Use deterministic event tie-breaking and document unavoidable signal-processing approximation. Maximum event count, queue capacity and CPU budget must be measured and frozen in the shared benchmark specification before a release gate passes.

### 3. Real-time and platform contract
Allocate and preprocess on UI/background thread or in prepareToPlay. No allocation, destruction of owning snapshot storage, mutex waits, file access, dynamic string formatting, logging, or UI calls in processBlock. Host adapter uses sample offsets clamped to current buffer with valid transport epochs; absent PPQ falls back to the documented manual tempo origin. Explicitly verify Windows MSVC and Linux GCC/Clang numerical behavior, standalone and VST3 lifecycle, state restore and CLAP capability when adapter is available.

### 4. User-facing workflow and accessibility
Quick mode exposes safe presets/preview, Editor mode exposes validated controls and revision management, Performance mode exposes only time-critical actions. Each control must have discoverable label and current value, keyboard operation, status feedback, appropriate automation and persisted settings. Preview never modifies the stored scene; commit is one undoable edit. Missing host capabilities are explained and do not silently alter scene state.

### 5. Verification plan and acceptance
All protected event IDs and beat spans unchanged after generation and morph; deny transformation when protected-state translation impossible.

Test F03-T01: fixed seed/input produces exact repeated output; F03-T02: malformed or unknown fields are rejected without state mutation; F03-T03: fixed-capacity queue saturation and stale generations preserve safe output; F03-T04: buffer sizes 1, 32, 256, 2048 and 44.1/48/96 kHz with tempo/seek changes; F03-T05: host project save/reopen and schema migration; F03-T06: audio rendering and latency/callback profiling when this feature touches sound; F03-T07: interface-to-application wiring and controller/automation behavior. Each fixture must retain expected values, target platform, command, commit SHA and pass/fail result. No feature is VERIFIED_COMPLETE on documentation alone.

### 6. Failure recovery, documentation and merge gate
On invalid edit, restore previous immutable version. On late planner publication, use last valid schedule. On overflow, increment a fixed-size status counter and apply documented priority. On missing dependency, visibly disable only the dependent function. Maintain user documentation, API contract and migration notes. Isolate changes by module, require targeted and cross-feature test results, and merge against refreshed integration head. Shared PluginProcessor, PluginEditor, state schema and CMake changes have one owner.
