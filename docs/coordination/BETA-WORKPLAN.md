# Beta workplan (2026-10-20)

Goal: implement every open spec/TODO item, add six new features, audit, QA, and publish a Beta
prerelease (Windows Standalone + VST3 + CLAP, macOS arm64 + Intel, Linux). Integration branch:
`claude/beta-integration`. Baseline: main `c83e8b2`, 29 core tests green.

## Rules for every worker

1. Read `AGENTS.md`, `docs/coordination/DECISIONS.md`, `docs/control-specs/SHARED-CONTRACTS.md` first.
2. **Spec first.** Write or complete the spec under `docs/specs/<ID>-<NAME>.md` (objective, exact
   parameters/ranges/defaults, state/serialization keys, RT behavior, UI wiring, acceptance tests with IDs,
   non-goals) BEFORE code. A feature is only done when every acceptance ID has a test or a recorded reason.
3. Keep domain logic portable in `src/core/<area>/` (no JUCE, fixed capacity, no allocation in the audio
   path). New UI goes in NEW files under `src/ui/`. Edits to shared hotspots (`PluginProcessor.*`,
   `InspectorPanel.cpp`, `PluginEditor.*`, `SceneSerializer.cpp`, `CMakeLists.txt`) must be small, additive
   and localized so merges stay trivial. Never reorder or reuse parameter IDs or scene keys; append only.
   Old saved states must still load (add a legacy-fixture test when you touch serialization).
4. Tests: add `tests/core/<Name>Tests.cpp` (auto-registered by CMake glob). Verify locally with
   `cmake -S . -B build-core -G Ninja -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release`
   then `cmake --build build-core -j2` and `ctest --test-dir build-core --output-on-failure -j2`.
   The machine has 2 cores and no JUCE system deps: JUCE/UI code cannot be built locally; write it carefully
   against JUCE 8.0.7 APIs (CI compiles it). Do not claim UI behavior you did not verify.
5. Docs: update `docs/alpha/ALPHA-FEATURE-STATUS.md` rows, `docs/TRACEABILITY.md`, and add a user help page
   under `help/` for user-visible changes. Do not edit `docs/TODO.md` (the integrator owns it).
6. Commit on your own branch (a worktree is provided). Do NOT push, do NOT merge, do NOT touch other
   workers' files. End every commit message with:
   `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and
   `Claude-Session: https://claude.ai/code/session_013hVywTiWbvtrvtzy6ahZHx`.
7. Final report (under 200 words): branch name, commit SHA, files touched in shared hotspots, tests added,
   test results, anything unverified or deferred. No claims of measurements you did not perform.

## Workers

| ID | Scope |
|---|---|
| W1 specs | F01-F12 specs (replace draft PR #6 content with specs matching reality + gaps), C01-C06 deviations, TODO coverage |
| W2 effects | Two new operations (Filter Sweep, Bit Crush) end to end |
| W3 dsp | C04 smoothing, Linkwitz-Riley split, loop-aware Reduce, Dry return, NaN safety, capacity, morph completeness |
| W4 loop | F09 pitch-preserving stretch, loop persistence in project, waveform data |
| W5 presets | Usability: factory and user preset browser, host programs |
| W6 euclid | Musical: Euclidean pattern generator and per-event chance |
| W7 print | Value: Print to WAV, drag out |
| W8 easy | Ease of use: Easy mode, one-knob Chaos macro, first-run guide |
| W9 release | CLAP format, Linux/macOS/Windows beta packaging workflow |
