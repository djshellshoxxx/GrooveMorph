# F05 — Phrase Evolution Engine

**Status:** DRAFT — interface not frozen. **Dependencies:** F01,F08. **Modules:** EvolutionPlanner.

## Scope and user value
Expose phrase evolution engine as an optional GrooveMorph capability driven by the existing canonical scenes, preserving manual editing and the base wet/dry workflow. Out of scope: cloud services, unbounded real-time machine learning and rewriting unrelated existing DSP.

## Requirements
- F05-FR-01: Accept validated input contract: base scene, seed, constraints, finite horizon, progression profile.
- F05-FR-02: Produce EvolutionTimeline with stable deterministic semantics and explicit invalid-input status.
- F05-FR-03: Preserve protected beats, existing scenes and parameter IDs unless user explicitly commits an edit.
- F05-NFR-01: Callback does bounded preallocated processing with no allocation/locks/I/O; background work is cancellable.
- F05-NFR-02: Persist only accepted canonical state with forward-version rejection and migration fixtures.

## Algorithm and lifecycle proposal
Precompute finite multi-cycle graph transitions from bounded mutation grammar; score continuity and density; generation runs off audio thread; freeze/commit or rollback each stage with lineage.

## Proposed public seam
```cpp
// Header/API to freeze once current repository types are reconciled.
struct F05Request; // validated input values: base scene, seed, constraints, finite horizon, progression profile
struct F05Result;  // immutable EvolutionTimeline; explicit status and generation
// Plan on a non-real-time thread; consume validated results through bounded handoff.
F05Result planF05(const F05Request& request) noexcept(false);
```

Explicitly bind input normalization, parameter min/max/default, buffer ownership, lifetime and error codes during M1 contract review. This declaration is illustrative and must not be copied as a working implementation without completing types. Serialized revisions include schema number, source scene IDs and deterministic seed when relevant.

## User workflow and conflicts
Start from editor or performance surface, configure finite options, preview or trigger, inspect pending status, accept/undo via HistoryManager. Host automation has documented precedence over local commands. Missing capabilities are signalled without destroying existing scene edits.

## Verification and acceptance
Finite planning horizon and time budget respected; same seed reproduces stages; stage regeneration does not mutate unselected history.

Required tests: F05-T01 deterministic model fixture, F05-T02 malformed range/state, F05-T03 late/overflow publication, F05-T04 host tempo/seek/buffer partition, F05-T05 restore/migration, F05-T06 production-quality audio review where DSP applicable. Create real fixture inputs, expected outputs and measurement tolerances before READY approval. Instrument CPU and allocation within the exact maximum configured project and document platform-specific behavior. QA must include no-signal, impulse, drum-loop, bass-heavy, mono/stereo and automation-storm contexts where applicable.

## Integration task contract
Implement module behind isolated portable core build target first; add adapter/UI wiring as separate reviewable commit; then update parameter manifest, state schema, preset migration, docs and traceability. Gate feature merge on passing unit+integration tests, callback-safety audit, regression render and target-host checks. Do not edit shared PluginProcessor concurrently.
