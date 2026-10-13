# GrooveMorph TODO and implementation ledger

Updated 2026-10-13. This ledger reflects the implemented runtime, not every feature
mentioned in a proposal. Source baseline: runtime 7f2784a, merged main aaa8dae;
feature audit examined main 61d5156. Before claiming ownership, fetch refs and
check existing PRs. Unassigned tasks below have no claimed implementation owner.

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

## Active work, dependencies and acceptance

| ID / priority | Status / owner | Next action | Required acceptance |
|---|---|---|---|
| A01 / P0 feature contract | Missing; unassigned | Specify and implement editable phrase origin, subdivision and bounded event lanes; then migrate scene schema/UI | Chosen interval alone is processed; visual/audio boundaries agree; independent scenes; legacy recall; loop/tempo/block fixtures |
| A02 / P0 release QA | Unverified; unassigned | REAPER Windows/Linux scan/load, MIDI, automation, project recall, PDC, device audio, listening, CPU/memory/deadline/allocation instrumentation | Record actual host/hardware/settings and thresholds; no inference from compilation/startup |
| A03 / P1 workflow bug | Reproduced; unassigned | Define requested versus active Variation semantics and safe host feedback | UI/MIDI/automation/cancel/reopen stay coherent without callback host notification |
| A04 / P1 MIDI conflict | Reproduced; unassigned | Reject/swap/warn on duplicate note assignments; define older-state recovery | No silently unreachable scene; state and undo preserve documented policy |
| A05 / P1 generator | Unmerged PR #9; confirm existing owner | Fix Linux PRNG type; replace assertion-only Release checks; verify core before UI preview/apply integration | Linux/Windows/sanitizers pass; tests remain effective under NDEBUG; seed/protection determinism and isolated preview |
| A06 / P1 authoring | Missing; unassigned | Define stable operation automation IDs, scene copy/duplicate controls and fade policy | Host automation/state/undo contract tested; copied scenes independent |
| A07 / P2 documentation/UX | Partial; unassigned | Native host screenshots + checked arrows/manifest, help entrypoint, effective slice-limit feedback, keyboard/scaling QA | Accurate clickable bounds and tested platforms; limitations visible |
| A08 / P2 expansion | Draft PR #6; confirm existing owner | Pin canonical baseline; complete/review F01–F12 interfaces, tasks, schemas, numerical fixtures | Package becomes READY only when measurable contracts and review evidence are complete |
| Distribution / P0 beta gate | Partial; unassigned | Publish verified VST3/Standalone bundles, installation guide, dependency/product license inventory, clean-machine check | Artifact/version/source match; install/remove paths work; no production claim from CI executable alone |

P0 feature contract means mandatory in the original target specification; it does
not mean the existing short-history engine fails to run. P0 QA is missing evidence,
not a missing DSP module. Priorities are from the feature audit; optional expansion
must not erase unmet original requirements or be treated as already delivered.

## Branch coordination

- PRs #2/#4/#7: merged/reconciled. Do not reinstall the superseded M0 processor.
- PR #6: spec/groovemorph-expansion-v1; INCOMPLETE, twelve draft features. Timing
  integration is resolved; specification depth/interface/state/test review remains.
- PR #9: feature/deterministic-variation-planner; core only, no user generator.
  Linux uint64_t-reference build failure reproduced; Windows green does not waive it.
- Seed storage, undo history and four-second audio history are not full F01, F08
  or F09 implementations. Morphing, groove protection, performance mode, evolution,
  polyrhythm, audio triggers, import/capture, multiband, gestures and CLAP are absent.

## Evidence and task closure

[Feature audit](feature-audit/FEATURE_AUDIT_2026-10-13.md) contains source locations,
reproduction outputs and next-action criteria. [Timing handoff](TIMING_INTEGRATION_HANDOFF.md)
contains exact passing CI/artifact links and limits. [Traceability](TRACEABILITY.md)
records requirement coverage. Close tasks only with relevant current tests and
user-facing wiring; update this ledger and help when behavior changes. No numeric
project completion percentage is asserted.

## Six-control specification package (2026-10-13)

- [x] Author [six detailed specs and shared integration plan](control-specs/README.md).
- [ ] Implement shared v2 scene/parameter/model prerequisites with legacy fixtures.
- [ ] Implement C01 placement/event authoring, then C03 permissions and C06 activity limits.
- [ ] Reconcile PR #9 into C02 distance/candidate workflow after Linux/Release-test repair.
- [ ] Extend canonical Scheduler for C04 phase and C05 automatic return.
- [ ] Wire six contextual inspector tabs and validate host/state/DSP/UI/RT acceptance.

Existing A01–A08 tasks remain; these specs elaborate them and do not mark the
separate F01–F12 package or features implemented.
