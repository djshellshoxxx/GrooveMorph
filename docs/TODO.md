# GrooveMorph TODO and implementation ledger

Updated 2026-10-13. Current integrated reference: main `055d0fd8` (PR #16).
This ledger reflects runtime behavior, not every proposed feature. The historical
feature audit examined earlier main `61d5156`; use the new QA ledger for current
release validation evidence. Check active PRs before claiming ownership.

## Completed

- [x] Reconcile Claude DSP/scenes/state with Codex PPQ calculations using one Scheduler.
- [x] Merge runtime to main (PR #7); preserve PR #2/#4 ancestry and canonical contracts.
- [x] Implement persisted Automatic / Host PPQ / Sample Clock selector and legacy recall.
- [x] Wire four scene editors, five operation controls, MIDI mapping, audition/compare and scene undo/redo.
- [x] Fix fractional PPQ boundaries, sub-block phase advancement, queued Manual button availability and stale audio after seeks.
- [x] Verify Windows/Linux VST3 and Standalone builds, portable ASan/UBSan and Windows startup at 7f2784a.
- [x] Run eight matching local Linux test executables successfully (2026-10-13).
- [x] Merge help pages and annotated source-derived SVG maps (PR #8).
- [x] Publish and merge feature audit (PR #11); record exact build evidence (PR #10).
- [x] Merge six musical control specs (PR #14) and portable deterministic variation-planning core (PR #9).
- [x] Add MIDI assignment collision diagnostic and tooltip (PR #15).
- [x] Integrate settings-only Copy FX with single-transaction undo (PR #17).
- [x] Correct reported host effect tail and demonstrate repeat-after-silence (PR #18).
- [x] Add Linux/Windows strictness-5 headless pluginval CI and portable 4-rate/3-block numerical quality fixtures (PR #16).
- [ ] Complete native host/device, GUI, objective audio/listening, RT deadline/allocations, deployment and licensing gates ([issue #19](https://github.com/djshellshoxxx/GrooveMorph/issues/19)).

## Active work, dependencies and acceptance

| ID / priority | Status / owner | Next action | Required acceptance |
|---|---|---|---|
| A01 / P0 feature contract | Missing; unassigned | Specify and implement editable phrase origin, subdivision and bounded event lanes; then migrate scene schema/UI | Chosen interval alone is processed; visual/audio boundaries agree; independent scenes; legacy recall; loop/tempo/block fixtures |
| A02 / P0 release QA | Automated pluginval Win/Linux and numerical core matrix pass PR #16; host/device QA open | Complete native REAPER Win/Linux, MIDI, automation, PDC, audio/listening, memory/deadline/alloc instrumentation in issue #19 | Archive host/hardware and thresholds; CI does not replace DAW compatibility evidence |
| A03 / P1 workflow bug | Reproduced; unassigned | Define requested versus active Variation semantics and safe host feedback | UI/MIDI/automation/cancel/reopen stay coherent without callback host notification |
| A04 / P1 MIDI conflict | Diagnostic warning implemented (PR #15); mapping policy still first-match | Decide whether to add auto-swap/reject; validate the warning in a native host | Duplicate assignments are visible, prior saved maps remain valid; first match keeps existing behavior |
| A05 / P1 generator | Portable planner merged (PR #9); UI absent | Design non-destructive preview/apply integration, seed and candidate controls | Existing planner Linux/Windows/sanitizer CI passed; seed/protection determinism and isolated preview remain required end-to-end |
| A06 / P1 authoring | Copy FX implemented/merged PR #17; full scene copy and automation still missing | Native host validate Copy FX/undo, define stable operation automation IDs, full-scene duplicate and fade policy | Settings copy independent and undoable; host parameter IDs unchanged |
| A07 / P2 documentation/UX | Partial; unassigned | Native host screenshots + checked arrows/manifest, help entrypoint, effective slice-limit feedback, keyboard/scaling QA | Accurate clickable bounds and tested platforms; limitations visible |
| A08 / P2 expansion | Draft PR #6; confirm existing owner | Pin canonical baseline; complete/review F01–F12 interfaces, tasks, schemas, numerical fixtures | Package becomes READY only when measurable contracts and review evidence are complete |
| Distribution / P0 beta gate | Engineering CI artifacts only; unassigned | Complete issue #19: versioned Win/Linux VST3/Standalone packages, install guide, product/third-party license review, clean-machine check | Artifact/source match; install/remove paths and licensing verified; no production claim from CI startup alone |

P0 feature contract means mandatory in the original target specification; it does
not mean the existing short-history engine fails to run. P0 QA is missing evidence,
not a missing DSP module. Priorities are from the feature audit; optional expansion
must not erase unmet original requirements or be treated as already delivered.

## Branch coordination

- PRs #2/#4/#7: merged/reconciled. Do not reinstall the superseded M0 processor.
- PR #6: spec/groovemorph-expansion-v1; INCOMPLETE, twelve draft features. Timing
  integration is resolved; specification depth/interface/state/test review remains.
- PR #9: merged deterministic planner core with regression tests after Linux/Release
  test repair; the end-user generator and C02 controls remain unimplemented.
- PR #14: six-control specification package merged; contract does not equal runtime.
- Seed storage, undo history and four-second audio history are not full F01, F08
  or F09 implementations. Morphing, groove protection, performance mode, evolution,
  polyrhythm, audio triggers, import/capture, multiband, gestures and CLAP are absent.

## Evidence and task closure

[Feature audit](feature-audit/FEATURE_AUDIT_2026-10-13.md) contains historical source locations,
reproduction outputs and next-action criteria. [Current QA ledger](qa/DAW_AUDIO_RELEASE_READINESS.md)
records validation gates and evidence. [Timing handoff](TIMING_INTEGRATION_HANDOFF.md)
contains original integration CI evidence and limits. [Traceability](TRACEABILITY.md)
records requirement coverage. Close tasks only with relevant current tests and
user-facing wiring; update this ledger and help when behavior changes. No numeric
project completion percentage is asserted.

## Six-control specification package (2026-10-13)

- [x] Author [six detailed specs and shared integration plan](control-specs/README.md).
- [ ] Implement shared v2 scene/parameter/model prerequisites with legacy fixtures.
- [ ] Implement C01 placement/event authoring, then C03 permissions and C06 activity limits.
- [ ] Integrate the merged PR #9 planner into C02 distance/candidate workflow with safe preview/apply and host/state tests.
- [ ] Extend canonical Scheduler for C04 phase and C05 automatic return.
- [ ] Wire six contextual inspector tabs and validate host/state/DSP/UI/RT acceptance.

Existing A01–A08 tasks remain; these specs elaborate them and do not mark the
separate F01–F12 package or features implemented.
