# GrooveMorph research and M0 progress

Research/specification details live in [`docs/SPECIFICATION.md`](docs/SPECIFICATION.md). Requirement coverage is tracked in [`docs/TRACEABILITY.md`](docs/TRACEABILITY.md).

## Approved direction (2026-10-10)

- Build architecture: C++20, JUCE, and CMake.
- Current outputs: VST3 and JUCE Standalone.
- CLAP is deferred unless specifically required by the owner after stack validation.
- Reference hosts: REAPER on Windows x64 and Linux x86_64.
- MVP direction: phrase variations and transition policy using bounded short-window rhythmic operations.
- Full phrase capture is out of scope for MVP; pitch-preserving stretch is deferred.
- Product differentiation is workflow-first rather than a feature-count race.

## Repository handoff state

At the start of this continuation, `main` was the only branch, with latest commit `4b8ed2e` (Windows build guide). There were no pull requests or GitHub Actions runs. The code was a JUCE shell with a dry-gain parameter; CMake declared a standalone GUI target referencing a `src/StandaloneMain.cpp` file absent from the repository tree. The editor implementation was split inconsistently between `PluginEditor.h` and `.cpp`, and no tests or CI workflow were present.

## M0 implementation in progress

- Added a JUCE-independent phrase clock that converts tempo/meter/phrase settings to sample-boundary offsets, with input validation and deterministic tests.
- Reworked the CMake model to offer VST3 and JUCE Standalone from the same processor and a separate core-only test mode.
- Reworked the processor shell to use an AudioProcessorValueTreeState parameter/state model and mono/stereo layout checks.
- Added portable-core CTest coverage and Windows/Linux GitHub Actions build jobs.
- Local verification available in this environment: direct GCC C++20 compilation with `-Wall -Wextra -Wpedantic -Werror` and the phrase-clock test executable. CMake is not installed locally; remote plugin build and DAW tests remain pending.

## M0 gate status

| Gate | Status | Evidence / next action |
|---|---|---|
| Portable phrase-clock tests | Passed locally | `g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror ...`; rerun via CTest in CI |
| CMake core-only configure | Not run locally | CMake unavailable; CI will verify |
| Windows VST3/Standalone build | Pending | GitHub Actions on `codex/m0-validation` |
| Linux VST3/Standalone build | Pending | GitHub Actions on `codex/m0-validation` |
| REAPER load/state/automation/PDC tests | Pending | Requires REAPER host on Windows and Linux |
| Standalone device/audio test | Pending | Requires desktop audio host/device |
| Callback allocation/thread audit | Pending | Static review now; add instrumentation/host test before M0 exit |

## Exit criteria

Do not begin broad feature implementation until Windows/Linux targets build, VST3 loads in REAPER on both, state/parameter behavior is verified, standalone is usable, zero-latency model is confirmed, and real-time safety has been reviewed. If a host or machine is unavailable, record the limitation and obtain an explicit fallback decision before claiming M0 complete.
