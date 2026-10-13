# GrooveMorph research and implementation progress

Updated 2026-10-13. The original 2026-10-10 research remains in
[the developer specification](docs/SPECIFICATION.md). Current evidence is in
[the integration handoff](docs/TIMING_INTEGRATION_HANDOFF.md),
[feature audit](docs/feature-audit/FEATURE_AUDIT_2026-10-13.md) and
[prioritized TODO list](docs/TODO.md).

## Approved direction

C++20, CMake and pinned JUCE 8.0.7; VST3 and JUCE Standalone on Windows x64/Linux
x86_64. REAPER is the reference host. The integrated effect uses four named
variations and five short-window operations with one canonical Scheduler.
Full phrase capture, pitch-preserving stretch and CLAP remain outside the current
runtime. Expansion direction is approved, but its technical package is incomplete.

## Delivered since the original M0 scaffold

The original shell/build inconsistencies and malformed .gitignore are repaired.
Main now contains the portable clock/resolver/scheduler, bounded-history DSP,
scene state, MIDI, undo/audition and wired editor controls. Timing selection is
Automatic, strict Host PPQ or Sample Clock, persisted in host state. PRs #2 and #4
are reconciled; PR #7 merged the runtime into main. PR #8 merged illustrated help.
Documentation PR #10 records exact verification; PR #11 adds the feature audit.

| Gate | Current evidence | Remaining work |
|---|---|---|
| Portable core, processor and editor callbacks | Eight matching Linux executables pass; Windows/Linux matrix passed at 7f2784a | Add regressions for audit findings |
| Windows/Linux VST3 + Standalone build | Verified CI at 7f2784a | Package distributable formats |
| ASan/UBSan portable tests | Verified CI at 7f2784a | Callback allocation/thread/deadline instrumentation |
| Windows startup | Process remained alive for five seconds; artifact uploaded | Actual device/audio and clean-machine install |
| REAPER scan/MIDI/automation/project/PDC | Not verified | Run Windows/Linux host matrix |
| DSP musical quality/click thresholds/CPU | Fixture tests exist; no completed target-hardware acceptance | Measured and listening QA |

## Current gaps and branch work

Missing original authoring contracts: editable phrase origin/subdivision and event
lanes. Scene-button/MIDI state is not synchronized back to the host Variation
parameter. Duplicate MIDI assignments silently use first-match lookup. Per-operation
host automation and scene copy controls are absent. Native help screenshots remain
outstanding; current SVG maps are diagrams, not screenshots.

PR #9 has an isolated deterministic planner, no user-facing generator wiring,
and a reproduced Linux uint64_t-reference compilation failure. Its assertion-only
checks disappear in Release. PR #6 contains twelve draft expansion feature specs;
its INCOMPLETE marker must be reconciled with the merged timing baseline before
readiness can be established. Neither branch is a delivered feature.

## Historical context

The early M0 scaffold and missing StandaloneMain.cpp described in older notes are
historical. Do not rebuild or merge that superseded processor over the canonical
runtime. Earlier research citations retain their original access dates; this
update does not imply fresh market research or new performance measurements.
