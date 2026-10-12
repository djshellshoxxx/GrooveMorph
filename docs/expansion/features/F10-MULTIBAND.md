# F10 — Multiband Transformations

**Status:** DRAFT — interface not frozen. **Dependencies:** F03,F09. **Modules:** MultibandRouter,TransitionMixer.

## Scope and user value
Expose multiband transformations as an optional GrooveMorph capability driven by the existing canonical scenes, preserving manual editing and the base wet/dry workflow. Out of scope: cloud services, unbounded real-time machine learning and rewriting unrelated existing DSP.

## Requirements
- F10-FR-01: Accept validated input contract: frequency crossover, filter mode, per-band scene handles.
- F10-FR-02: Produce BandPlan with stable deterministic semantics and explicit invalid-input status.
- F10-FR-03: Preserve protected beats, existing scenes and parameter IDs unless user explicitly commits an edit.
- F10-NFR-01: Callback does bounded preallocated processing with no allocation/locks/I/O; background work is cancellable.
- F10-NFR-02: Persist only accepted canonical state with forward-version rejection and migration fixtures.

## Algorithm and lifecycle proposal
Start from phase-coherent minimum-phase crossover topology validated by summed response; align band delays and latency; permit per-band operations with documented unavoidable transient/crossover artifacts; defer linear phase until latency budget validated.

## Proposed public seam
```cpp
// Header/API to freeze once current repository types are reconciled.
struct F10Request; // validated input values: frequency crossover, filter mode, per-band scene handles
struct F10Result;  // immutable BandPlan; explicit status and generation
// Plan on a non-real-time thread; consume validated results through bounded handoff.
F10Result planF10(const F10Request& request) noexcept(false);
```

Explicitly bind input normalization, parameter min/max/default, buffer ownership, lifetime and error codes during M1 contract review. This declaration is illustrative and must not be copied as a working implementation without completing types. Serialized revisions include schema number, source scene IDs and deterministic seed when relevant.

## User workflow and conflicts
Start from editor or performance surface, configure finite options, preview or trigger, inspect pending status, accept/undo via HistoryManager. Host automation has documented precedence over local commands. Missing capabilities are signalled without destroying existing scene edits.

## Verification and acceptance
Impulse reconstruction, sweep sum, stereo correlation, aliasing, discontinuity, CPU and PDC checks at all supported sample rates.

Required tests: F10-T01 deterministic model fixture, F10-T02 malformed range/state, F10-T03 late/overflow publication, F10-T04 host tempo/seek/buffer partition, F10-T05 restore/migration, F10-T06 production-quality audio review where DSP applicable. Create real fixture inputs, expected outputs and measurement tolerances before READY approval. Instrument CPU and allocation within the exact maximum configured project and document platform-specific behavior. QA must include no-signal, impulse, drum-loop, bass-heavy, mono/stereo and automation-storm contexts where applicable.

## Integration task contract
Implement module behind isolated portable core build target first; add adapter/UI wiring as separate reviewable commit; then update parameter manifest, state schema, preset migration, docs and traceability. Gate feature merge on passing unit+integration tests, callback-safety audit, regression render and target-host checks. Do not edit shared PluginProcessor concurrently.
