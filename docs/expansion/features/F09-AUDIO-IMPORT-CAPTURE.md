# F09 — Audio Pattern Import and Phrase Capture

**Status:** DRAFT — interface not frozen. **Dependencies:** F08. **Modules:** AudioImportService,AudioHistory.

## Scope and user value
Expose audio pattern import and phrase capture as an optional GrooveMorph capability driven by the existing canonical scenes, preserving manual editing and the base wet/dry workflow. Out of scope: cloud services, unbounded real-time machine learning and rewriting unrelated existing DSP.

## Requirements
- F09-FR-01: Accept validated input contract: validated URI/file handle, channel policy, duration and storage budget.
- F09-FR-02: Produce AudioAssetHandle with stable deterministic semantics and explicit invalid-input status.
- F09-FR-03: Preserve protected beats, existing scenes and parameter IDs unless user explicitly commits an edit.
- F09-NFR-01: Callback does bounded preallocated processing with no allocation/locks/I/O; background work is cancellable.
- F09-NFR-02: Persist only accepted canonical state with forward-version rejection and migration fixtures.

## Algorithm and lifecycle proposal
Decode/resample/import asynchronously into bounded owned assets, normalize paths, calculate digest, and atomically publish handles; capture uses preallocated audio ring buffer; missing referenced assets produce user-visible fallback.

## Proposed public seam
```cpp
// Header/API to freeze once current repository types are reconciled.
struct F09Request; // validated input values: validated URI/file handle, channel policy, duration and storage budget
struct F09Result;  // immutable AudioAssetHandle; explicit status and generation
// Plan on a non-real-time thread; consume validated results through bounded handoff.
F09Result planF09(const F09Request& request) noexcept(false);
```

Explicitly bind input normalization, parameter min/max/default, buffer ownership, lifetime and error codes during M1 contract review. This declaration is illustrative and must not be copied as a working implementation without completing types. Serialized revisions include schema number, source scene IDs and deterministic seed when relevant.

## User workflow and conflicts
Start from editor or performance surface, configure finite options, preview or trigger, inspect pending status, accept/undo via HistoryManager. Host automation has documented precedence over local commands. Missing capabilities are signalled without destroying existing scene edits.

## Verification and acceptance
Malformed/truncated file corpus fails without callback I/O; sample-rate/channel changes tested; host state policy explicitly identifies embedded/reference/package choice.

Required tests: F09-T01 deterministic model fixture, F09-T02 malformed range/state, F09-T03 late/overflow publication, F09-T04 host tempo/seek/buffer partition, F09-T05 restore/migration, F09-T06 production-quality audio review where DSP applicable. Create real fixture inputs, expected outputs and measurement tolerances before READY approval. Instrument CPU and allocation within the exact maximum configured project and document platform-specific behavior. QA must include no-signal, impulse, drum-loop, bass-heavy, mono/stereo and automation-storm contexts where applicable.

## Integration task contract
Implement module behind isolated portable core build target first; add adapter/UI wiring as separate reviewable commit; then update parameter manifest, state schema, preset migration, docs and traceability. Gate feature merge on passing unit+integration tests, callback-safety audit, regression render and target-host checks. Do not edit shared PluginProcessor concurrently.
