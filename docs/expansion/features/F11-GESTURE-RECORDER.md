# F11 — Gesture Recording and Playback

Status: DRAFT, not implementation ready. Depends on stable SceneModel, EventScheduler, StateCodec and existing MVP integration.

## Scope and required behavior
Gesture recording must be sample-accurate, support quantized/free input, editable playback speed, loop and explicit priority against host automation; empty scene ref must not trigger arbitrary default scene

## Proposed module boundaries
GestureTimeline + TriggerRouter. Public input: Bounded timeline containing beat-time/host-sample offsets, command type, scene UUID/revision, overdub policy and deterministic command ordering. Expose a versioned immutable result and explicit errors; the exact C++ signatures, enum identity, memory ownership and parameter IDs require freezing after baseline reconciliation. State edits take place off audio thread. All playback uses bounded preallocated event queues, with deterministic overflow and cancellation.

## Functional requirements
- F11-FR-01: Configurable inputs, ranges and defaults are validated before publication.
- F11-FR-02: Operations are deterministic from same revision, transport timeline and controller/input history.
- F11-FR-03: No temporary performance action modifies edited scene snapshots without acceptance.
- F11-FR-04: Feature parameters retain identity and host state compatibility.

## DSP / persistence / lifecycle contract
All callback paths are bounded, nonallocating, nonblocking and I/O free. Store schema versions, scene references and logical timestamps, reject invalid future revisions without partial restore, and discard stale worker generations. Background management handles allocation, edit history, file operations and UI event feedback. Define exact timebase, queue capacity and version migration before implementing.

## QA and acceptance
Replay across buffer partitions and transport seek, tempo changes and loop boundaries remains deterministic; overflow and invalid scene refs are recoverable. Add F11-T01 deterministic fixture, F11-T02 corrupt schema, F11-T03 high-rate automation and controller input, F11-T04 host seek/loop/tempo, F11-T05 Windows/Linux host-compatibility checks. Collect sample rate, buffer size, processor data, expected output hash and observed result. Engineering review must calibrate measurable numeric tolerances rather than invent them.

## Integration tasks
Freeze interfaces, create portable core tests, implement isolated module, add format/editor bridge without concurrent edits to PluginProcessor, document migration and failure modes, run full host and callback safety tests, then update traceability. Block feature completion until evidence is recorded.
