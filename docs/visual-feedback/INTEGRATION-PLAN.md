# Visual feedback: integration plan

Companion to [SPEC.md](SPEC.md). Baseline `main` 3ace82d. Owner of this workstream: the session that opened PR #22
(record any hand-over in `docs/TODO.md`). Do not overwrite the canonical Scheduler/DSP/scene/state runtime.

## 1. Coordination rules

- Check open PRs/branches and `git log -- src/core/dsp src/PluginProcessor.*` before each slice (as of this writing: none in flight; PR #6 is incomplete and out of scope).
- Branching: this session is restricted to `claude/affectionate-hopper-5naq7k` (PR #22). Each slice is one or more separate, individually tested commits on that branch, prefixed `viz(S<n>):`. Another owner may instead use short-lived `viz/<slice-id>-<topic>` branches off the latest `main`, one PR per slice. Either way: ≤5 source files or ≈300 meaningful lines per slice; split otherwise.
- Tests first (RED commit), then implementation; existing tests are never weakened. Integrate a green slice promptly (squash merge by the user/owner; this workstream does not merge without instruction).
- Engine changes (S2/S3/S5) carry the bit-exactness test (VF-T02) in the same PR.
- Public behaviour changes update `docs/TODO.md`, `docs/TRACEABILITY.md`, `help/`, and `docs/alpha/*` status in the same PR or the immediately following docs PR.

## 2. Slices (dependency order)

| Slice | Content | Files (new/changed) | Tests | Depends on |
|---|---|---|---|---|
| S0 | This spec + plan (PR #22) | `docs/visual-feedback/*` | links/JSON check | none |
| S1 | `TraceFrame`, `TraceRing` (portable) | `src/core/trace/TraceFrame.h`, `TraceRing.h/.cpp`; `tests/core/TraceRingTests.cpp` | VF-T01 | S0 |
| S2 | `StageGeometry` extraction from `RhythmEngine` (behaviour-preserving) | `src/core/dsp/StageGeometry.h/.cpp`, `RhythmEngine.cpp`; `tests/core/StageGeometryTests.cpp` | VF-T05 + all existing engine tests bit-exact | S1 |
| S3 | Engine sink + epoch + peaks + processor ownership of the ring | `RhythmEngine.h/.cpp`, `PluginProcessor.h/.cpp`; `tests/core/TraceEngineTests.cpp` | VF-T02, VF-T03, VF-T10 | S1 (S2 recommended first) |
| S4 | `PathResolver` + `WeaveLayout` (pure) | `src/core/viz/*`; `tests/core/PathResolverTests.cpp`, `WeaveLayoutTests.cpp` | VF-T04, VF-T06 | S3 |
| S5 | `PlannedPath` using `StageGeometry` | `src/core/viz/PlannedPath.*` | planned vs engine equality (extends VF-T05) | S2, S4 |
| S6 | `ViewModel`/`Ledger` text + status snapshot (pure) | `src/core/viz/Ledger.*`, `ViewModel.*` | VF-T07 | S4 |
| S7 | Visual window shell + `WeaveView` + `LedgerView`; header **Visual** button; tracing on/off; prefs | `src/ui/visual/*`, `PluginEditor.*` | VF-T08, VF-T09 | S4, S6 |
| S8 | `StationsView`, `OrbitView`, glyphs, idle/preview/invalid states | `src/ui/visual/*` | VF-T08 | S7 |
| S9 | `RingsView` + lap diff | `src/ui/visual/*`, `src/core/viz/LapDiff.*` | lap-diff goldens | S7 |
| S10 | Accessibility, reduced motion, CVD/greyscale review, small-screen layout, help page | `src/ui/visual/*`, `help/*` | VF-T08 additions | S8, S9 |
| S11 | Native validation (Windows/REAPER, issue #19): visuals vs audible behaviour, CPU open/closed, Standalone vs VST3 | `docs/qa/*` | recorded evidence | S10 |
| S12 (optional) | Per-stage peaks, repeat/self-similarity map | engine + views | new fixtures | S11 |

S1→S3 and S4→S6 form two parallelisable chains after S1/S3; the UI (S7+) needs both.

## 3. Verification gates per slice

1. `cmake -S . -B build-core -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON && cmake --build build-core --config Release && ctest --test-dir build-core --build-config Release --output-on-failure` (all prior tests stay green).
2. Full plugin build + ctest on Linux; CI: Windows/Linux builds, portable core, ASan/UBSan, pluginval, Windows standalone smoke.
3. Slices touching threads (S1, S3): add a TSan run of the ring stress test locally; note in the PR if TSan was not available.
4. Slices touching the callback (S3): run the allocation-instrumented fixture; record the offline per-block cost.
5. UI slices: render fixtures with `tools/EditorSnapshot` (extended) and attach images; check 1100×680 and 800×500.
6. Windows alpha packaging workflow (`windows-alpha.yml`) is run after S7 and S10 to produce a testable Standalone.

Never claim: DAW compatibility, real-time deadline margins, audio quality, or visual accuracy beyond what tests and recorded measurements show.

## 4. Requirement traceability (to be copied into `docs/TRACEABILITY.md` by S3/S7)

| Req | Statement | Slices | Evidence |
|---|---|---|---|
| VF-01 | Bit-identical audio with sink null/inactive/active | S3 | VF-T02 |
| VF-02 | Bounded callback cost, no alloc/lock/I/O | S3 | VF-T02/T03, allocation fixture, benchmark note |
| VF-03 | Portable core, JUCE only in `src/ui/visual` | all | include audit (grep) in CI-lite script |
| VF-04 | No visual state in project/scene state | S7 | state round-trip test unchanged |
| VF-05 | Closed window ⇒ tracing off | S7 | VF-T09 |
| VF-06 | Text equivalent, reduced motion, colour-independent | S8, S10 | VF-T07/T08 + review |
| VF-07 | No Gross Beat likeness | S10 | side-by-side review checklist |
| VF-08 | Planned and measured drawn distinctly and consistent with engine | S5 | VF-T05 |
| VF-09 | Idle/invalid/preview states honest | S8 | state fixtures |

## 5. Risks and rollbacks

- **Engine refactor (S2)** is the riskiest change: gated by sample-exact equality vs the pre-refactor engine (golden captured before the change). Rollback = revert S2; later slices only need `StageGeometry` for planned paths (S5), not for tracing.
- **Visual window in hosts**: secondary windows can be restricted; fallback inline panel (SPEC §10). Rollback = hide the header button via a compile-time flag.
- **Trace overhead**: sink checked per sample only via a hop modulus; if the benchmark exceeds budget, increase `hopSamples` (constant, one place).
- **Merge conflicts** with other sessions editing `PluginEditor`/`PluginProcessor`: keep edits to a few anchor lines (member + button + ring pointer); put everything else in new files.

## 6. Definition of done (feature)

All slices S1–S10 merged with green CI; S11 evidence recorded in `docs/qa/`; user-facing help page; `docs/alpha` status updated;
anti-resemblance review (VF-07) signed off by the user. Items not verified natively remain listed as unverified in the status docs.
