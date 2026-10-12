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
