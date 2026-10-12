# F04 — Live Performance Mode

**Status:** DRAFT — interface not frozen. **Dependencies:** F02,F12. **Modules:** TriggerRouter,CommandBridge.

## Scope and user value
Expose live performance mode as an optional GrooveMorph capability driven by the existing canonical scenes, preserving manual editing and the base wet/dry workflow. Out of scope: cloud services, unbounded real-time machine learning and rewriting unrelated existing DSP.

## Requirements
- F04-FR-01: Accept validated input contract: note/CC/UI command, timestamp, priority, mode.
- F04-FR-02: Produce ScheduledPerformanceCommand with stable deterministic semantics and explicit invalid-input status.
- F04-FR-03: Preserve protected beats, existing scenes and parameter IDs unless user explicitly commits an edit.
- F04-NFR-01: Callback does bounded preallocated processing with no allocation/locks/I/O; background work is cancellable.
- F04-NFR-02: Persist only accepted canonical state with forward-version rejection and migration fixtures.

## Algorithm and lifecycle proposal
Route mapped MIDI and UI actions to a bounded priority queue; support Hold Fill and Return to Base with ownership of temporary scene state, momentary latch edge semantics, trigger quantization and emergency dry.

## Proposed public seam
```cpp
// Header/API to freeze once current repository types are reconciled.
struct F04Request; // validated input values: note/CC/UI command, timestamp, priority, mode
struct F04Result;  // immutable ScheduledPerformanceCommand; explicit status and generation
// Plan on a non-real-time thread; consume validated results through bounded handoff.
F04Result planF04(const F04Request& request) noexcept(false);
```

Explicitly bind input normalization, parameter min/max/default, buffer ownership, lifetime and error codes during M1 contract review. This declaration is illustrative and must not be copied as a working implementation without completing types. Serialized revisions include schema number, source scene IDs and deterministic seed when relevant.

## User workflow and conflicts
Start from editor or performance surface, configure finite options, preview or trigger, inspect pending status, accept/undo via HistoryManager. Host automation has documented precedence over local commands. Missing capabilities are signalled without destroying existing scene edits.

## Verification and acceptance
Simultaneous triggers have deterministic priority; disconnect safely retains base state; override never mutates stored scene; emergency dry operates even if scheduling is unavailable.

Required tests: F04-T01 deterministic model fixture, F04-T02 malformed range/state, F04-T03 late/overflow publication, F04-T04 host tempo/seek/buffer partition, F04-T05 restore/migration, F04-T06 production-quality audio review where DSP applicable. Create real fixture inputs, expected outputs and measurement tolerances before READY approval. Instrument CPU and allocation within the exact maximum configured project and document platform-specific behavior. QA must include no-signal, impulse, drum-loop, bass-heavy, mono/stereo and automation-storm contexts where applicable.

## Integration task contract
Implement module behind isolated portable core build target first; add adapter/UI wiring as separate reviewable commit; then update parameter manifest, state schema, preset migration, docs and traceability. Gate feature merge on passing unit+integration tests, callback-safety audit, regression render and target-host checks. Do not edit shared PluginProcessor concurrently.
