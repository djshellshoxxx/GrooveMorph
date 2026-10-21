# GrooveMorph

GrooveMorph is a beta-stage phrase-aware rhythmic audio effect. It provides four editable variations, seven rhythmic transformations, selectable musical/sample timing, presets, Euclid patterns, Print to WAV and an Easy mode.

## Current approved direction

- C++20, CMake, and JUCE
- VST3, JUCE Standalone and CLAP targets
- Windows x64, Linux x86_64 and macOS build targets (CLAP and macOS are CI-built only)
- REAPER as the first reference host on both operating systems
- Phrase variations and quantized transitions using bounded short-window operations
- Optional pitch-preserving loop stretch (WSOLA) is built; it has not been listened to

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

The full VST3, Standalone and CLAP targets fetch JUCE 8.0.7 on first configuration. See [GETTING_STARTED.md](GETTING_STARTED.md) for platform dependencies and target names.

## Wiki and verification

A navigation wiki is in [`docs/wiki/`](docs/wiki/Home.md). In a documentation pass on Linux, the portable-core commands above were run as written: configure, build and `ctest` reported 56 of 56 tests passing. The JUCE targets, CLAP and DAW compatibility were not built or tested in that pass.

## Current status

Updated 2026-10-20 (beta). Read the [TODO](docs/TODO.md) for open work (B-01 to B-11), the
generated [completeness matrix](docs/specs/COMPLETENESS-MATRIX.md) for spec, test, code and UI per requirement,
[feature status](docs/alpha/ALPHA-FEATURE-STATUS.md), [known issues](docs/alpha/KNOWN-ISSUES.md) and the
[release readiness matrix](docs/qa/DAW_AUDIO_RELEASE_READINESS.md).

The portable core has automated tests that pass on Linux. The JUCE targets are built by GitHub
workflows. Nobody has yet tested the beta in a DAW, on audio hardware, by listening, with a screen
reader, or loaded the CLAP in a CLAP host, and some editor and processor changes are compiled in CI
only. The product licence is not chosen (see [third-party notices](THIRD-PARTY-NOTICES.md)).
**Passing CI does not mean production or DAW certification.**
Track native QA in [issue #19](https://github.com/djshellshoxxx/GrooveMorph/issues/19), which is open.
