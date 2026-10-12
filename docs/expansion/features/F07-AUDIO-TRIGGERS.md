# F07 — Audio-Triggered Transitions

**Status:** DRAFT — interface not frozen. **Dependencies:** F04. **Modules:** AnalysisWorker,TriggerRouter.

## Scope and user value
Expose audio-triggered transitions as an optional GrooveMorph capability driven by the existing canonical scenes, preserving manual editing and the base wet/dry workflow. Out of scope: cloud services, unbounded real-time machine learning and rewriting unrelated existing DSP.

## Requirements
- F07-FR-01: Accept validated input contract: sidechain capability, envelope settings, thresholds, refractory samples.
- F07-FR-02: Produce TriggerEvent with stable deterministic semantics and explicit invalid-input status.
- F07-FR-03: Preserve protected beats, existing scenes and parameter IDs unless user explicitly commits an edit.
- F07-NFR-01: Callback does bounded preallocated processing with no allocation/locks/I/O; background work is cancellable.
- F07-NFR-02: Persist only accepted canonical state with forward-version rejection and migration fixtures.

## Algorithm and lifecycle proposal
Calculate RMS/peak envelope with bounded state, hysteresis, hold/refractory interval and attack/release; route detected events through normal trigger priority; expose missing sidechain fallback; optional offline calibrated confidence.

## Proposed public seam
```cpp
// Header/API to freeze once current repository types are reconciled.
struct F07Request; // validated input values: sidechain capability, envelope settings, thresholds, refractory samples
struct F07Result;  // immutable TriggerEvent; explicit status and generation
// Plan on a non-real-time thread; consume validated results through bounded handoff.
F07Result planF07(const F07Request& request) noexcept(false);
```

Explicitly bind input normalization, parameter min/max/default, buffer ownership, lifetime and error codes during M1 contract review. This declaration is illustrative and must not be copied as a working implementation without completing types. Serialized revisions include schema number, source scene IDs and deterministic seed when relevant.

## User workflow and conflicts
Start from editor or performance surface, configure finite options, preview or trigger, inspect pending status, accept/undo via HistoryManager. Host automation has documented precedence over local commands. Missing capabilities are signalled without destroying existing scene edits.

## Verification and acceptance
False-trigger and missed-trigger fixtures, silence and impulses, varying buffer sizes; no lookahead claims without measured reporting latency.

Required tests: F07-T01 deterministic model fixture, F07-T02 malformed range/state, F07-T03 late/overflow publication, F07-T04 host tempo/seek/buffer partition, F07-T05 restore/migration, F07-T06 production-quality audio review where DSP applicable. Create real fixture inputs, expected outputs and measurement tolerances before READY approval. Instrument CPU and allocation within the exact maximum configured project and document platform-specific behavior. QA must include no-signal, impulse, drum-loop, bass-heavy, mono/stereo and automation-storm contexts where applicable.

## Integration task contract
Implement module behind isolated portable core build target first; add adapter/UI wiring as separate reviewable commit; then update parameter manifest, state schema, preset migration, docs and traceability. Gate feature merge on passing unit+integration tests, callback-safety audit, regression render and target-host checks. Do not edit shared PluginProcessor concurrently.
