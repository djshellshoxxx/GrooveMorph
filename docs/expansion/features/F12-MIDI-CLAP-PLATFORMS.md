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
