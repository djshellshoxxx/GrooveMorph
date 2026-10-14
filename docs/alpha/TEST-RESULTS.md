# Test results (alpha)

Only checks that were actually run are listed. Dates 2026-10-14.

## Local, Linux x86_64 (alpha 2)
| Check | Result |
|---|---|
| Portable core, GCC Release: 14 test executables (adds SceneControl, CandidateBuilder, EnginePhase, OnsetDetector, LoopSource) | 14/14 passed |
| Portable core with `-fsanitize=address,undefined` | passed, no reports |
| Full plugin build (VST3 + Standalone + tests), GCC and Clang (`-Wall -Wextra`) + `ctest` | 15/15 passed, no warnings from project sources |
| Processor tests include: host Variation sync, placement boundaries at misaligned block sizes, auto-return (bars and phrases) landing on the bar line, band split (50 Hz / 5 kHz projections), audio trigger, MIDI macros, loop capture/playback locked to the bar grid (also with placement splitting segments), file import validation, gesture record/play, phrase evolution never touching stored/saved scenes, scene operations keeping locks | passed |
| Independent code audit (3 read-only reviewers: real-time/concurrency, core correctness, GUI/state) | findings fixed or documented; see below |

Audit follow-ups fixed in this build: heap allocation in the activity limiter (`std::stable_sort` replaced), capture/loop source rendered with a pre-clamp block length when placement split a segment, loop double-buffer reuse race and 200 ms stalls (reader flag handshake), capture cancel/abort races (compare-exchange), message-thread loop services vs `prepareToPlay` (mutex), locked-event merge dropping locked events and loop lengths, coverage math for events beyond the phrase, phantom/garbled events from hostile scene files, detector start-up trigger and subnormal envelopes, demo noise depending on block splits, placement lock leaving controls disabled, locks not applied to Copy FX/Duplicate/Reset/history recall/previews, selection tracked by array index, Convert changing limit priorities, rounded depth/slice written on Enable toggles, seed corruption, Undo during a preview, truncated imports.

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
