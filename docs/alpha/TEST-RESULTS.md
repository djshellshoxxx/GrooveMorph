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

## Windows (GitHub Actions, windows-2022, MSVC, Release)
Alpha build: [run 38497756566](https://github.com/djshellshoxxx/GrooveMorph/actions/runs/38497756566) on `main` @ `4042b61`
(release `v0.1.0-alpha.3`). PR head `33c927b` checks, all passed: Windows + Linux portable core, JUCE plugin/standalone build,
Windows + Linux pluginval (pinned, strictness 5), Linux ASan/UBSan, Windows Standalone smoke.

| Check | Result |
|---|---|
| Compile + link `GrooveMorph.exe` (PE32+ GUI x86-64) | passed (an earlier commit failed on a `var` name clash with `juce::var` under MSVC; fixed in `33c927b`) |
| `ctest` on Windows | 12/12 passed (`ctest-windows.log` in the ZIP) |
| Standalone startup | process still alive after 8 s on a runner with no audio device |
| Desktop screenshot | captured on the runner (`docs/alpha/screenshots/windows-ci-desktop-1024x768.png`) |
| ZIP | SHA-256 `3737447f...cca9` matches the `.sha256` asset; contains exe, docs, manifest, ctest log, screenshot |

What the real screenshot shows (and does not): the real Windows rendering of the editor at 1024x768. The 1240x740 window is cropped by that
small desktop (see KNOWN-ISSUES), the status line reads "audio not running" (no audio device on the runner), and JUCE's yellow
"input muted to avoid feedback loop" banner is shown. It is a startup screenshot only; no controls were operated on Windows.

## Not performed
DAW hosting (REAPER or other), VST3 validator run on the new build, audio-device playback, listening/quality evaluation,
real-time deadline and allocation instrumentation, long-session stress, interactive GUI tests on a Windows desktop by the author,
CPU measurements. Core/processor tests do not prove DAW compatibility or audio quality.
