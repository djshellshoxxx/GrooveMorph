# GrooveMorph

GrooveMorph is a phrase-aware rhythmic audio effect. The integrated MVP provides four editable variations, five rhythmic transformations and selectable musical/sample timing.

## Current approved direction

- C++20, CMake, and JUCE
- VST3 and JUCE Standalone targets
- Windows x64 and Linux x86_64 as provisional build targets
- REAPER as the first reference host on both operating systems
- CLAP deferred unless the owner reopens that decision
- Phrase variations and quantized transitions using bounded short-window operations
- Full phrase capture and pitch-preserving time stretch deferred

Agent workflows are available in [skills/README.md](skills/README.md): full Superpowers, ten DSP/JUCE skills, and token-efficient communication skills without Antigravity protocols. See [AGENTS.md](AGENTS.md) and the [completion-session prompt](docs/COMPLETION_PROMPT.md) for the current branch-integration handoff.

## User help

Read the [illustrated GrooveMorph user guide](help/README.md) for first-run setup, a complete control reference, timing modes, editing and preview, MIDI workflows, troubleshooting, and annotated interface diagrams. The current diagrams are source-derived and explicitly not native screenshots; see the [screenshot capture checklist](help/VISUAL-GUIDE.md).

## Quick start

See [the specification](docs/SPECIFICATION.md), [research and milestone notes](RESEARCH_NOTES.md), and [requirement traceability](docs/TRACEABILITY.md).

## Build and test

Portable-core build and tests do not require JUCE:

```sh
cmake -S . -B build-core -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

The full VST3 and Standalone targets fetch JUCE 8.0.7 on first configuration. See [GETTING_STARTED.md](GETTING_STARTED.md) for platform dependencies and target names.

## Current status

The code includes portable timing, scheduling, bounded-history DSP, scene state and MIDI modules. The editor exposes variation editing, audition/compare, undo/redo and Automatic / Host PPQ / Sample Clock selection. CI results and DAW validation are recorded in pull requests and release notes when available. A passing core test does not establish plugin-host compatibility.
