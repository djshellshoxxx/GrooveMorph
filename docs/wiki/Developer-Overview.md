# Developer Overview

## What it is

A C++20 project with a portable core and a JUCE shell. The core (`src/core/`) builds as a static library (`GrooveMorphCore`) with no JUCE dependency, and its tests run without JUCE. The shell (`src/PluginProcessor.*`, `src/PluginEditor.*`, `src/ui/`) is the JUCE VST3 and Standalone target, named `GrooveMorphPlugin`. Version `0.1.0` in `project()`.

## Design decisions (and why)

**Portable core, JUCE at the edge.** `src/core/` holds the scheduling, scene, variation, DSP and trace logic. It can be built, tested and sanitised on Linux without JUCE (`GROOVEMORPH_CORE_ONLY_BUILD`). The JUCE processor is an adapter between the host and the core.

**Bar-line switching through a scheduler.** Variation changes are queued and applied on a bar boundary, with sample accuracy (`Scheduler`, `PhraseClock`, `TransportResolver`). A switch that arrives mid-bar waits for the next legal point, so the audio does not jump.

**Phase per operation.** `RhythmTiming` carries an optional phase offset for each operation, so operations can have their own grid origin. With zero offsets it falls back to the shared grid.

**Scenes are data, with undo.** Variations are `Variation` and `SceneSnapshot` records in `SceneBank`. Edits go through an undo stack (`scene/UndoStack`). `SceneSerializer` writes them as state, and the host parameter IDs are stable (`GM-FR-007`, never repurposed).

**Controlled change.** `control/` holds the rules for how a generated or morphed variation may change the audio: protected regions, locks, placement evaluation, gesture lanes, and automatic return. Generated changes are checked against these before they apply.

**Audition is isolated.** `scene/AuditionSession` lets you preview a candidate without changing the committed variation.

**Trace for the UI.** `core/trace/TraceRing` is a lock-free ring of frames that the audio side writes and the UI reads, so the visual windows (rings, stations, weave, ledger) do not block the audio thread.

**Deterministic generation.** Variation generation is seeded and reproducible (`variation/VariationPlanner`, `CandidateBuilder`). Golden tests (`EngineGoldenTests`) pin the engine output.

**Honest status in docs.** The alpha status file lists what is tested and what is not, with the words *Complete and tested*, *Implemented, partially tested* and so on. Keep to those words.

## Signal flow

```
host audio in ──► GrooveMorphAudioProcessor::processBlock()
                     │
                     ├─ TransportResolver (host PPQ or manual or sample clock)
                     ├─ PhraseClock ──► RhythmTiming (samplesPerBeat, beatAtBlockStart, per-op phase)
                     ├─ Scheduler (queued variation switches on bar line, transition policy, auto-return)
                     ├─ SceneControl / VariationBlend / GestureLane (controlled changes, morph)
                     ├─ RhythmEngine (Repeat, ReverseSlice, Gate, HalfTime, TapeStop per voice)
                     ├─ dry/wet mix, Intensity, band protect (Protect lows) and downbeat protect
                     ▼
host audio out
                     ▲
MIDI in ─► MidiLinks / MidiNoteMap ─► scene changes, CC macros
UI (ui/, visual/) ◄─ TraceRing frames, SceneBank snapshots (read only)
```

## Threads

- Audio thread: `processBlock()` and everything it calls in `core/` (scheduler, rhythm engine, trace writes). No allocation, no locks.
- Message thread: editor, undo, scene edits, generator, file I/O. Edits are handed to the audio side as parameter or scene state, not by touching the running engine.

## Data

- `OperationSettings`, `OperationType`, `operationCount`, `variationCount = 4` (`core/OperationParams.h`)
- `Variation`, `SceneSnapshot`, `SceneBank` (`core/scene/SceneBank.h`)
- `SwitchEvent`, `Scheduler` (`core/Scheduler.h`)
- `ProtectedRegion`, `PlacementConfig`, `LaneEvent`, `EventLane`, `SceneControl` (`core/control/SceneControl.h`)
- `RhythmTiming`, `RhythmEngine` (`core/dsp/RhythmEngine.h`)
- `TraceRing`, `TraceFrame` (`core/trace/`)

See [[Developer-Code-Map]] for locations and [[Developer-Conventions]] for the rules.
