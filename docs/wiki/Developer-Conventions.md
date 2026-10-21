# Developer Conventions

## Style

- C++20. `namespace groovemorph::core` for the portable core.
- Mark pure value helpers `noexcept` where they are used on the audio thread, and `[[nodiscard]]` where a dropped result is a bug.
- Use the status words from `docs/alpha/ALPHA-FEATURE-STATUS.md` when you describe a feature.

## Real-time rules

- The audio thread runs `processBlock()` and everything under `src/core/` it calls. No allocation, no locks, no file I/O there.
- UI and trace data cross the thread boundary through `TraceRing` (atomics) or by scheduled state changes. Do not let the UI reach into the running engine.
- Variation switches are scheduled to the bar line. Do not apply a scene change immediately from the UI thread.

## Portability

- Nothing under `src/core/` may include JUCE headers. The core-only CI build will fail if it does.
- JUCE types live only in `src/PluginProcessor.*`, `src/PluginEditor.*`, `src/ui/`. Colour values for the Visual window come from `src/core/viz/Palette.h` as portable constants.

## Stable IDs and saved state

- Host parameter IDs are stable (`GM-FR-007`). Never reuse or rename one.
- Scene state goes through `SceneSerializer`. Changing it needs a migration note and a test in `tests/core/SceneSerializerTests.cpp`.

## Honesty in docs

- Do not claim a feature is complete unless tests exist and the alpha status says so. The status file has a list of unfinished items. Keep it in step with the code.
- `docs/SPECIFICATION.md` is the target. `docs/TRACEABILITY.md` links requirements to tests.

## How to add a feature

1. Check the spec (`docs/SPECIFICATION.md`) and `docs/TODO.md`.
2. Put the logic in `src/core/` with a standalone test in `tests/core/`.
3. Wire it through the processor (`src/PluginProcessor.cpp`) and the UI if needed.
4. Run the core build and `ctest`. Then run the full build.
5. Update `docs/alpha/ALPHA-FEATURE-STATUS.md` and `docs/TRACEABILITY.md`.

## Known limitations

From `README.md` and `docs/alpha/KNOWN-ISSUES.md`:

- Not tested in a DAW or with audio hardware yet.
- Full phrase capture and pitch-preserving time stretch are deferred.
- CLAP is deferred. VST3 and Standalone only.
- Some phrase and event-lane features are partly implemented (see the alpha status).
