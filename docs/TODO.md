# GrooveMorph TODO and implementation ledger

Updated 2026-10-20 for the beta branch. This ledger says what runs, what was tested and what
nobody has checked yet. It does not claim a release. The per-requirement join of spec, test,
code and UI is generated into [the completeness matrix](specs/COMPLETENESS-MATRIX.md)
(`python3 shared/scripts/spec_matrix.py`); every B item below is referenced from it.

**What "tested" means here:** the portable core has automated tests that pass on Linux, and the
GitHub workflows build the JUCE targets. Nobody has loaded GrooveMorph in a DAW for this beta,
run it on audio hardware, listened to the output, used it with a screen reader, or loaded the CLAP
in a CLAP host. Native host and device QA is [issue #19](https://github.com/djshellshoxxx/GrooveMorph/issues/19)
and it stays **open**. Several UI and processor edits in the beta were written on a machine that
cannot build JUCE; they are compiled by CI only and are listed in B-01 and B-02.

## Completed in the beta (code, with tests where stated)

- [x] Runtime: four scenes, seven operations (Filter Sweep and Bit Crush added, N01), one Scheduler,
  Automatic / Host PPQ / Sample Clock, MIDI, audition, scene undo, host state.
- [x] Phrase placement and bounded event lanes (A01/C01, capacity 256), permissions (C03), start phase (C04) and
  per-scene smoothing, automatic return and Dry target (C05), activity limits and loop-aware Reduce (C06, F06).
- [x] Generator with locks, seed, style, distance and event patterns (F01/C02), local history with a tested core codec (F08).
- [x] Morphing (F02), downbeat protection and Linkwitz-Riley crossover (F03/F10), performance macros and scene
  shortcuts 1-4, T, Esc (F04), phrase evolution (F05), audio trigger (F07), loop import with optional stretch and
  project-saved loop (F09/W4), gesture recorder with a write threshold (F11).
- [x] Presets (N02), Euclid and chance (N03), Print to WAV (N04), Easy mode and first-run guide (N05).
- [x] MIDI note swap dialog (A04 as built by W10), operation live trims `op<N>Enable/Depth/Slice` (A06 as built by W10).
- [x] CLAP target through clap-juce-extensions, packaging scripts and `beta-release.yml` (N06). Built in CI only.
- [x] Visual window (Weave, Ledger, Rings, Stations) and its core.
- [x] Spec completeness audit: every spec acceptance row has an audited status; spec files carry an as-built block.
- [x] Docs aligned: GM-QR-012 amended for Print to WAV, CLAP in scope, `operationCount` is 7.

## Remaining work

Each item has an acceptance criterion so it can be closed without argument.

| ID | Item | Acceptance |
|---|---|---|
| B-01 | **Native DAW, device and UI QA (issue #19, open).** REAPER on Windows, Linux and macOS; VST3, Standalone and CLAP; automation, MIDI, PDC, state round trip with a large loop, listening checks, screen reader pass, CPU and callback deadline and allocation measurement, keyboard-only pass, scene shortcuts typed in a real window | A written record per host/OS/format with versions, buffer sizes, devices and outcomes under `docs/qa/`; rows marked MANUAL in the matrix move to a tested status or stay with a reason |
| B-02 | **Processor-level tests that need the JUCE build** (F02-T06, F04-T09, F05 locks/audition/seek, F06-T07, F07-T05/T10, F11, A06-T07, bandTopology legacy restore, C04 smoothing, C05 same-sample return) | Tests added to `tests/PluginProcessorTests.cpp` or `W3ProcessorTests.cpp`, run green in CI |
| B-03 | **Licence and notices.** Product licence decision (GM-DEC-10) is the owner's and not made. No `LICENSE`, no notice generator, no About view, no SBOM, notices not embedded in binaries | Owner decision recorded; `LICENSE` added; generator fails on unknown components; About view; SBOM attached to the release |
| B-04 | **UI gaps:** loop ring not drawn on the lane canvas, UI scale setting, F1 help, capped-slice indicator, phase preview, Ctrl/Cmd+Z in the editor, Tab/Alt+arrow lane editing | Each built and shown in a screenshot or manual record; A07 and F06-T08 rows updated |
| B-05 | **MIDI policy:** reject or warn CC 20/21/22 links while macros are on, skip macro slots, describe shared CCs in the menu, soft pickup for CC links (A04-T07..T09, F12-T11) | Core tests for the policy, UI text, rows updated |
| B-06 | **Deliberate non-goals for the beta:** three or more bands (F10-T08), multiple gesture lanes and resolution above 20 Hz (F11-T08), evolution over several scenes and step preview (F05-T09) | Build them or keep them out; the specs say so either way |
| B-07 | **Audio trigger tuning:** rendered drum, bass-heavy and dense-sustained fixtures with a measured retrigger rate (F07-T09) | Fixtures committed, thresholds recorded |
| B-08 | **History inside the DAW project** (F08-T08). Kept a local file by decision | Only if the owner asks; then a spec change plus tests |
| B-09 | **Classic 12 dB/oct split has no portable test** (F03-T06). The code is in `PluginProcessor.cpp` | Move into core or add a processor test |
| B-10 | **Human reviews:** no-Gross-Beat-likeness design review, colour-blind review of the Visual window | Written sign-off |
| B-11 | **First beta-release workflow run:** four-leg build, clap-validator, `lipo -archs`, checksums, notices in each archive. CLAP never loaded in a CLAP host | A successful run read by a person, plus B-01 CLAP host result |

## Not delivered and not claimed

Audio quality, latency figures, CPU figures and DAW compatibility are unmeasured. Pitch-preserving
stretch is an optional WSOLA mode, not studio quality, and has not been listened to. The saved loop
is 16-bit. No production or release claim is made.

## Where the evidence lives

[Completeness matrix](specs/COMPLETENESS-MATRIX.md) (generated), [spec index](SPEC_INDEX.md),
[alpha/beta feature status](alpha/ALPHA-FEATURE-STATUS.md), [known issues](alpha/KNOWN-ISSUES.md),
[QA ledger](qa/DAW_AUDIO_RELEASE_READINESS.md), [decisions](coordination/DECISIONS.md),
[timing handoff](TIMING_INTEGRATION_HANDOFF.md), [traceability](TRACEABILITY.md) and the older
[feature audit](feature-audit/FEATURE_AUDIT_2026-10-13.md) (historical, from before the beta work).
Close a task only with a current test or a recorded manual result, and update this file.

- B-12: On the headless macOS CI runner the W3 test harness's console message loop does not deliver async messages, so W3-TM-T03 (service start) and W3-TM-T02 (timer ticks) are reported SKIPPED there. They pass on Linux and Windows. Not verified in a real macOS host; check in a DAW during native QA (issue #19).
