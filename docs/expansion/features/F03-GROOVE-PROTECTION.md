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
