# F06 — Polyrhythmic and Independent Operation Lanes

**Status:** DRAFT — interface not frozen. **Dependencies:** F08. **Modules:** MusicalTime,EventScheduler.

## Scope and user value
Expose polyrhythmic and independent operation lanes as an optional GrooveMorph capability driven by the existing canonical scenes, preserving manual editing and the base wet/dry workflow. Out of scope: cloud services, unbounded real-time machine learning and rewriting unrelated existing DSP.

## Requirements
- F06-FR-01: Accept validated input contract: rational lane period, phase, anchor, loop epoch.
- F06-FR-02: Produce BoundedEventPlan with stable deterministic semantics and explicit invalid-input status.
- F06-FR-03: Preserve protected beats, existing scenes and parameter IDs unless user explicitly commits an edit.
- F06-NFR-01: Callback does bounded preallocated processing with no allocation/locks/I/O; background work is cancellable.
- F06-NFR-02: Persist only accepted canonical state with forward-version rejection and migration fixtures.

## Algorithm and lifecycle proposal
Store lane periods as reduced rational ticks with overflow-checked arithmetic; schedule next events inside finite lookahead horizon, never construct unbounded LCM; re-anchor on seek and meter change.

## Proposed public seam
```cpp
// Header/API to freeze once current repository types are reconciled.
struct F06Request; // validated input values: rational lane period, phase, anchor, loop epoch
struct F06Result;  // immutable BoundedEventPlan; explicit status and generation
// Plan on a non-real-time thread; consume validated results through bounded handoff.
F06Result planF06(const F06Request& request) noexcept(false);
```

Explicitly bind input normalization, parameter min/max/default, buffer ownership, lifetime and error codes during M1 contract review. This declaration is illustrative and must not be copied as a working implementation without completing types. Serialized revisions include schema number, source scene IDs and deterministic seed when relevant.

## User workflow and conflicts
Start from editor or performance surface, configure finite options, preview or trigger, inspect pending status, accept/undo via HistoryManager. Host automation has documented precedence over local commands. Missing capabilities are signalled without destroying existing scene edits.

## Verification and acceptance
Counterexample periods with huge LCM remain bounded; deterministic across host buffer sizes, meter changes, loop wraps and seeks.

Required tests: F06-T01 deterministic model fixture, F06-T02 malformed range/state, F06-T03 late/overflow publication, F06-T04 host tempo/seek/buffer partition, F06-T05 restore/migration, F06-T06 production-quality audio review where DSP applicable. Create real fixture inputs, expected outputs and measurement tolerances before READY approval. Instrument CPU and allocation within the exact maximum configured project and document platform-specific behavior. QA must include no-signal, impulse, drum-loop, bass-heavy, mono/stereo and automation-storm contexts where applicable.

## Integration task contract
Implement module behind isolated portable core build target first; add adapter/UI wiring as separate reviewable commit; then update parameter manifest, state schema, preset migration, docs and traceability. Gate feature merge on passing unit+integration tests, callback-safety audit, regression render and target-host checks. Do not edit shared PluginProcessor concurrently.
