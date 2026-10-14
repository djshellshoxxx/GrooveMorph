# Test results (alpha)

Only checks that were actually run are listed. Dates 2026-10-14.

## Local, Linux x86_64 (GCC, Release, JUCE 8.0.7)
| Check | Result |
|---|---|
| Portable core: `cmake -DGROOVEMORPH_CORE_ONLY_BUILD=ON`, 11 test executables (incl. new SceneControlTests, CandidateBuilderTests) | 11/11 passed |
| Portable core with `-fsanitize=address,undefined` | 11/11 passed, no reports |
| Full plugin build (VST3 + Standalone + tests) + `ctest` (core tests + GrooveMorphPluginTests) | 12/12 passed |
| New processor tests: host Variation sync (pending/cancel/GUI/unread automation), placement exactly dry before a LastBar window and engaging at/after sample 288000 with a deliberately misaligned 300-sample block, auto-return landing at sample 96000, duplicate/reset/undo, state round-trip of control data, Standalone free-run clock | passed |
| Linux Standalone launched under Xvfb | stayed alive for the 10 s timeout (no audio device in the container, ALSA seq warning only) |
| Editor rendered by `tools/EditorSnapshot.cpp` (software rendering of the real editor) | PNGs in `docs/alpha/screenshots/` (placement, generator, scenes, morph, limits, status, minimum size) |

## Windows (GitHub Actions `windows-alpha.yml`, windows-2022)
See BUILD-MANIFEST.md in the ZIP and the workflow run linked in the final report for the actual compile, `ctest`, startup-smoke and screenshot results of the exact commit.

## Not performed
DAW hosting (REAPER or other), VST3 validator run on the new build, audio-device playback, listening/quality evaluation,
real-time deadline and allocation instrumentation, long-session stress, interactive GUI tests on a Windows desktop by the author,
CPU measurements. Core/processor tests do not prove DAW compatibility or audio quality.
