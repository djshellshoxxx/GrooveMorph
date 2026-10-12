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
