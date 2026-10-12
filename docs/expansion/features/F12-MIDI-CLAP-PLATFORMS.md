# F12 — MIDI, Host Format and Platform Expansion

Status: DRAFT, not implementation ready. Depends on stable SceneModel, EventScheduler, StateCodec and existing MVP integration.

## Scope and required behavior
One shared DSP core wrapped by VST3, Standalone and separately verified CLAP adapter; soft pickup and conflict detection; preserve host automation and state across formats

## Proposed module boundaries
MIDIMapper + AutomationBridge + host adapters. Public input: Timestamped MIDI Note/CC/pressure/pitch, capability flags and existing stable APVTS IDs. Expose a versioned immutable result and explicit errors; the exact C++ signatures, enum identity, memory ownership and parameter IDs require freezing after baseline reconciliation. State edits take place off audio thread. All playback uses bounded preallocated event queues, with deterministic overflow and cancellation.

## Functional requirements
- F12-FR-01: Configurable inputs, ranges and defaults are validated before publication.
- F12-FR-02: Operations are deterministic from same revision, transport timeline and controller/input history.
- F12-FR-03: No temporary performance action modifies edited scene snapshots without acceptance.
- F12-FR-04: Feature parameters retain identity and host state compatibility.

## DSP / persistence / lifecycle contract
All callback paths are bounded, nonallocating, nonblocking and I/O free. Store schema versions, scene references and logical timestamps, reject invalid future revisions without partial restore, and discard stale worker generations. Background management handles allocation, edit history, file operations and UI event feedback. Define exact timebase, queue capacity and version migration before implementing.

## QA and acceptance
Native Linux/Windows host plugin scan, parameter automation/preset reopen, controller tests, runtime installer checks, CLAP validator and pinned JUCE/adapter/license audit. Add F12-T01 deterministic fixture, F12-T02 corrupt schema, F12-T03 high-rate automation and controller input, F12-T04 host seek/loop/tempo, F12-T05 Windows/Linux host-compatibility checks. Collect sample rate, buffer size, processor data, expected output hash and observed result. Engineering review must calibrate measurable numeric tolerances rather than invent them.

## Integration tasks
Freeze interfaces, create portable core tests, implement isolated module, add format/editor bridge without concurrent edits to PluginProcessor, document migration and failure modes, run full host and callback safety tests, then update traceability. Block feature completion until evidence is recorded.

## Owner-approved implementation contract supplement (2026-10-12)

The proposed interface direction is approved. Existing SceneBank, Scheduler, TransportResolver, JUCE APVTS IDs and current persistence retain precedence until compatible migrations are implemented and tested.

### Data and algorithm
Normalize MIDI note/CC/pressure, host position and automation into canonical timestamped events while preserving the existing APVTS parameter IDs. Version and validate controller mapping and soft takeover. Wrap portable core with format-specific adapters; pin and validate JUCE/CLAP adapter integration independently before publishing CLAP binaries.
Input contract: immutable canonical scene revision, validated timebase, bounded commands and explicit host capabilities. Output contract: immutable generation-stamped plan with status enum (valid, invalid input, missing capability, stale generation, overflow), deterministic event ordering by sample offset, priority and sequence. Enforce finite numeric values, bounded timeline length and known scene references before publishing. Parameter defaults and external schema IDs must be frozen against source-level manifests; do not reinterpret an existing project state.

### Execution and lifecycle
Idle -> Recording/Planning -> Preview -> Accepted, with Cancelled/Failed preserving previous committed state. Audio callback may read fixed-size validated plans and atomics but cannot allocate, free shared storage, lock, perform I/O, log, or perform UI work. Producer owns dynamic vectors, mappings and history; consumer acknowledges generations and any reclamation occurs off the audio thread. Invalid transport falls back to explicit manual-time mode; loops and seeks increment timeline epochs.

### UI and host integration
Quick mode exposes minimum safe controls; Editor mode provides detailed gesture/mapping data; Performance mode provides live trigger and clear pending status. Keyboard navigation, labels, controller reconnection and host automation conflicts must be supported. Persist only committed settings and versioned data with undo/redo and corruption recovery.

### Test matrix and acceptance
Windows and Linux VST3, Standalone and CLAP host scan, automation, latency, MIDI and state reopen must each be independently verified; no binaries are ready on documentation alone. Required fixtures: F12-T01 exact replay/determinism, T02 malformed/future state, T03 queue overflow, T04 tempo/meter/loop/seek, T05 standalone/VST3 host serialization, T06 Linux/Windows compatibility, T07 UI/MIDI/automation wiring. Record command, commit SHA, target, expected output, measured result and reviewer. No unverifiable numeric performance promises: determine budgets using baseline benchmarks and approve before release.

### Integration handoff
Implement in a portable module first, integrate with approved command bridge under single PluginProcessor owner, add state migration and GUI adapter, run core and host regressions, then merge a reviewed PR. Preserve existing worker branches.
