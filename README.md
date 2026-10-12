# GrooveMorph

GrooveMorph is a phrase-aware rhythmic audio effect. The repository is in active MVP implementation; the current source is a timing/state foundation, not a finished product.

## Approved direction

- C++20, CMake, and JUCE 8.0.7
- VST3 and JUCE Standalone targets
- Windows x64 and Linux x86_64
- REAPER as the first reference host
- CLAP, full phrase capture, and pitch-preserving stretch deferred
- Four phrase variations, bounded rhythmic operations and explicit transitions in the approved MVP

See [the specification](docs/SPECIFICATION.md), [research and milestone notes](RESEARCH_NOTES.md), [spec review](docs/SPEC_REVIEW.md), and [requirement traceability](docs/TRACEABILITY.md).

## Build and test

Portable core build and tests do not require JUCE:

```sh
cmake -S . -B build-core -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

The full VST3 and Standalone targets fetch JUCE 8.0.7 on first configuration. See [GETTING_STARTED.md](GETTING_STARTED.md) for platform dependencies.

## Current status

The continuation branch fixes initial timing/state edge cases and adds a Windows Standalone smoke/artifact path. The main rhythmic DSP, scenes, scheduler, triggers, and complete editing workflow are still under development. Green compile/test jobs do not establish DAW compatibility or audio quality.
