# Developer Guide

## Portable core build

This needs no JUCE. The commands are the project baseline in [AGENTS.md](../../AGENTS.md):

```sh
cmake -S . -B build-core -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON
cmake --build build-core --config Release
ctest --test-dir build-core --build-config Release --output-on-failure
```

The verification pass on Linux configured, built and ran these commands: 56 of 56 CTest tests passed.

## Full targets

VST3, Standalone and CLAP fetch JUCE 8.0.7 on first configure. Platform dependencies and target names are in [GETTING_STARTED.md](../../GETTING_STARTED.md). CLAP is built in CI only and has not been loaded in a CLAP host.

## Rules that matter

- The audio callback has bounded work. It does not allocate, block, do I/O, log or touch the UI.
- Domain code under `src/core/` stays portable. JUCE and platform code stays in the adapter and UI layers.
- Public behaviour changes need a specification update first. See [docs/specs](../specs/COMPLETENESS-MATRIX.md) and [docs/TRACEABILITY.md](../TRACEABILITY.md).
