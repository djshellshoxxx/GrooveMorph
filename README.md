# GrooveMorph

GrooveMorph is an alpha-stage phrase-aware rhythmic audio effect. The integrated MVP provides four editable variations, five rhythmic transformations and selectable musical/sample timing.

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

Updated 2026-10-14. Alpha status: [feature status](docs/alpha/ALPHA-FEATURE-STATUS.md), [known issues](docs/alpha/KNOWN-ISSUES.md), [user test guide](docs/alpha/USER-TEST-GUIDE.md). Review the [prioritized TODO list](docs/TODO.md),
[release readiness and DAW acceptance matrix](docs/qa/DAW_AUDIO_RELEASE_READINESS.md),
and [feature audit](docs/feature-audit/FEATURE_AUDIT_2026-10-13.md).
The integrated runtime and modular improvements are on main. PR #9's portable
deterministic variation planner is merged, but its generator/GUI workflow is not
implemented. PR #14's six musical-control specifications are merged and the
controls are partly implemented in the alpha (see docs/alpha/ALPHA-FEATURE-STATUS.md). PRs #15–#18 added MIDI collision diagnostics,
undoable Copy FX, bounded host effect-tail reporting and cross-platform pluginval
plus numerical audio regressions. PR #6 remains an incomplete draft specification
workspace and is intentionally unmerged.

Both Windows and Linux VST3 pass headless pluginval strictness level 5 in GitHub
Actions. The CI checks omit GUI tests and the standalone Steinberg SDK validator.
Native REAPER/project recall, audio-device playback, listening/alias analysis,
CPU/deadline/heap instrumentation, product licensing and deployment QA remain
unverified. **Passing CI does not mean production or DAW certification.**
Track these gates in [issue #19](https://github.com/djshellshoxxx/GrooveMorph/issues/19).
