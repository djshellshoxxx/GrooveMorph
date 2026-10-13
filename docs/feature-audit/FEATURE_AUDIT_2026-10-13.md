# GrooveMorph feature audit, 2026-10-13

## Scope and conclusion

Inspected main commit `61d5156e2fef01fa83193581c93cb8e2357049a4`, original
`docs/SPECIFICATION.md`, current editor/processor/core, tests, CI, help, issue #5,
and open PRs #6 and #9. Runtime matches verified integration commit `7f2784a`;
the intervening main changes are documentation and an audit-agent definition.
This audit changes documentation only and does not merge or implement features.

GrooveMorph delivers a working short-history rhythmic effect with four editable
scenes, five transformations, selectable PPQ/sample timing, quantized requests,
MIDI, preview/compare, scene undo, bypass and host state. It does **not** yet deliver
the specification's full phrase/event editor or its F01–F12 expansion. A saved
seed and a serializer are infrastructure, not evidence of a usable generator or
external preset browser. Green automated tests do not establish native DAW or
listening acceptance.

Two main-branch workflow gaps were reproduced: scene-button selection leaves the
host Variation parameter unchanged, and duplicate MIDI assignments silently make
a later variation unreachable through that note. PR #9's unmerged generator also
has a reproduced Linux compiler error and Release-disabled assertion tests.

## Status definitions

- **Verified:** end-to-end path exists and its automated fixture passed. This is
  not a claim that every platform/DAW acceptance check passed.
- **Implemented, untested:** code exists but the relevant use case was not run.
- **Partial:** some promised behavior exists; a control, model, integration or
  acceptance requirement remains missing.
- **Infrastructure only:** reusable code/state exists without a delivered user workflow.
- **Absent/deferred:** no delivered implementation; explicitly deferred scope is
  not counted as a defect in the existing short-history effect.

## Core feature traceability

| Requirement / feature | Status | Expected versus actual behavior and precise source | Test evidence / confidence |
|---|---|---|---|
| FR-001 phrase length and meter | Verified, partial overall | Processor.cpp:63–76 exposes manual BPM 20–300, numerator 1–16, denominators 1/2/4/8/16 and phrase bars 1–16. Editor.cpp:57–83 attaches them. Manual grid at Processor.cpp:323–333 always supplies sample origin 0. No origin/subdivision parameter or editor. | PhraseClockTests, TransportResolverTests; high |
| Phrase visualization / lanes | Partial | Editor.cpp:326–337 draws bar divisions and a cursor. There are no beat/subdivision ticks, operation event rectangles, arbitrary event editing or zoom. OperationParams.h:23–29 stores one uniform enable/depth/slice group per operation, not an event sequence. | Source/UI inspection; high |
| FR-002 four named variations | Verified, partial event contract | SceneBank.h:33–81 owns four names/settings; Editor.cpp:84–113 provides performance buttons and independent edit target/name. Isolation and state work; independent rhythmic **event data** and copy/duplicate controls are absent. | SceneBankTests, serializer/processor tests; high |
| FR-003 repeat/stutter | Verified | RhythmEngine.cpp:234–243 and Editor.cpp:114–135 implement short prior-slice repeats with enable/depth/slice controls. Fixed operation order; bounded history. | DspRhythmEngineTests repeat fixture; high |
| FR-003 reverse slice | Verified | RhythmEngine.cpp:244–250 reads a causal prior slice backwards; editor controls and scene state are wired. This is not full-phrase reverse. | Core reverse fixture and seek-history regressions; high |
| FR-003 gate | Verified | RhythmEngine.cpp:316–325 applies rhythmic gate with ramps; UI and persistence exist. | Core gate and processor MIDI-split phase fixture; high |
| FR-003 half-time feel | Implemented; partial acceptance | RhythmEngine.cpp:251–257 implements half-speed causal playback, not pitch-preserving stretch. UI/state exist. General bounds/finite/determinism tests do not prove musical quality. | Core general operation fixtures; native listening remains untested; high for code, medium for perceived effect |
| FR-003 tape stop | Implemented; partial acceptance | RhythmEngine.cpp:258–266 uses a slowdown rate curve; UI/state exist. No user rate-curve editor. | Core finite/bounds/discontinuity fixtures; musical listening untested; high/medium |
| FR-004 scheduled transitions | Verified | Scheduler.cpp:30–114 and Processor.cpp:379–439 handle Immediate, Beat, Bar, Phrase and Manual; requests split processing at the boundary. Latest pending request wins. No user fade-duration control. | SchedulerTests, processor timestamp/countdown/MIDI tests; high |
| FR-005 intensity and operation depth | Verified | Processor.cpp:58–62,78–82,372–375 combine intensity × dry/wet. Editor.cpp:114–135 edits operation settings. Engine applies smoothing and dry path. | Core dry-null, processor dry/bypass fixtures; high |
| FR-006 sync/fallback selection | Verified | timingMode values 0 Automatic / 1 Host PPQ / 2 Sample Clock are parameters at Processor.cpp:95–97. Resolver validates host fields; strict PPQ holds requests without required fields. Explicit Sample Clock preview free-runs without host play state; stopped host stays stopped. | Clock/resolver/scheduler/processor tests including odd meter, loops, 137-BPM fractional boundaries and legacy mode migration; high |
| FR-007 host automation | Partial | Ten APVTS parameters at Processor.cpp:54–98 provide global control/variation automation. Per-operation enable/depth/slice are scene state, not automation parameters. UI/MIDI requests alter Scheduler but do not update host Variation value; see A03. | Global state and variation scheduling tests; reproduced UI readback mismatch; high |
| FR-008 MIDI note triggering | Verified with mapping gap | Processor.cpp:194–212 consumes note timestamps; MidiNoteMap.cpp:5–35 implements first-match map and latest-held-note return policy. Editor.cpp:221–230 edits mapping. Duplicates are accepted silently; no MIDI-learn/channel filter/output. | Mapping/timestamp fixtures; duplicate-note probe; high |
| FR-009 preview/compare | Verified | Editor.cpp:137–152,232–238 and Processor.cpp:552–610 connect stored/edited preview, apply and revert. AuditionSession.cpp holds isolated working copy; editor close ends preview. | SceneUndoAuditionTests and real editor callback fixture; high |
| FR-009 undo/redo | Verified within scene scope | UndoStack.cpp:15–46 stores bounded bank snapshots. Processor.cpp:522–550 and Editor.cpp:153–154 wire controls. Global APVTS tempo/intensity/transition changes are not part of this scene undo history. | Core and processor undo equality tests; high |
| FR-010 host/project state | Verified, partial host acceptance | Processor.cpp:623–697 embeds versioned scene text and APVTS values. Oversized/invalid envelopes are ignored; malformed scene text recovers with report. Legacy absent timingMode restores Automatic. No product preset-browser/file workflow. | SceneSerializerTests and processor round trips/recovery; actual DAW save/reopen untested; high |
| FR-011 status | Verified within shipped scope | Editor.cpp:241–307 shows timing source/missing fields, active/queued labels, countdown, phrase position, preview and received MIDI event count. Absence of events is not a proven host routing failure. No capture/analysis status because those engines are absent. | Processor assertions, editor callback/snapshot evidence; high |
| FR-012 bypass / dry-wet / latency | Verified, partial host acceptance | Processor.cpp:214–270,274–283 implements dry bypass ramp; prepare reports zero algorithmic latency. Mono/stereo layouts accepted. No independent host PDC/validator proof. | Processor bypass ramp/dry/layout/latency tests; high |
| Bounded audio history / seek reset | Verified | RhythmEngine.h:40–65 defines four-second history, per-operation clamp and O(1) invalidation. Processor.cpp:335–363 detects sample/PPQ discontinuities. Slice tooltips disclose limits, but no live requested-versus-effective duration indicator. | Core and processor seek-isolation tests; high |
| FR-013 transient/grid analysis | Absent/deferred | No analysis/confidence/manual-marker module or UI. Host/manual timing is not audio tempo detection. | Repository inventory; high |
| FR-014 downbeat/low-frequency protection | Absent/deferred | No protected-time region, crossover, low-frequency mask or UI in main. Operation protection in PR #9 preserves settings, not audio downbeats/bass. | Repository inventory and PR #9 code; high |
| FR-015 controlled generation / seed | Infrastructure only in main | SceneBank.h:66–67, SceneSerializer.cpp:172 and Processor.cpp:514–520 preserve/edit a seed. RhythmEngine never consumes it. No generation/style/strength/seed controls. PR #9 adds an unmerged planner with no processor/editor call site. | Seed persistence tests; PR #9 fails Linux compilation; high |
| FR-016 standalone scene-bank files | Absent/deferred | String serializer exists, but no file dialog, atomic write/import service, browser or external-bank control. Host-managed presets remain possible through the DAW. | Source inventory and program API at Processor.h:60–64; high |
| FR-017 audio/sidechain triggers | Absent/deferred | Processor exposes only matching mono/stereo main input/output. MIDI is the only event-trigger adapter implemented. | Bus-layout tests and source inventory; high |
| FR-018 full phrase capture | Absent/deferred | Short live history is implemented; no capture/arm/commit workflow, audio asset, long capture storage or latency mode. | RhythmEngine.h:40–50; high |
| Help/documentation | Partial | Seven help pages plus three SVG control maps are merged. help/README.md and VISUAL-GUIDE.md label maps as source-derived diagrams. Native screenshot/arrow set and in-plugin help entrypoint are absent. Software-rendered snapshot used during integration is not a native host screenshot. | Help file inventory/source inspection; high |
| Windows/Linux VST3 and Standalone | Verified build; partial distribution | Pinned JUCE 8.0.7; CI builds both formats on both OSes. Windows workflow uploads only Standalone executable. No published VST3 package/release installer workflow in repository. | Successful integration CI and artifact SHA; high |
| CLAP / MIDI output / macOS | Absent/deferred | CMake declares VST3/Standalone, no MIDI output. Current approved platform target is Windows/Linux; other formats/platforms are future work. | CMake/plugin declarations; high |

Source paths above omit the common `src/` prefix for processor/editor and `src/core/`
for portable modules. Lines refer to the inspected main commit. Test presence is
not substituted for test execution; executed checks are listed below.

## Ranked gaps, acceptance criteria and dependencies

### A01 — P0 feature contract: phrase origin, subdivisions and event lanes

The current product's phrase length mostly drives quantized scene changes and the
cursor; operations repeat uniformly over their slice unit. It cannot place a reverse
only on beat four or a gate only in the final bar. This is the largest discrepancy
between the promised phrase editor and delivered workflow (FR-001/002, spec §12).

Next action: specify a bounded phrase-event model, stable musical origin/subdivision,
versioned state migration and editor interactions before implementation. Depend on
the canonical PhraseClock/Scheduler, not a second processor. Acceptance: edit an
operation at one chosen phrase location; hear exactly that interval; display and
scheduler agree; changing one scene leaves others untouched; old states migrate;
all host/sample modes, loops and variable blocks remain covered. Keep expansion
F06 polyrhythms separate from the initial single-grid lane contract.

### A02 — P0 release gate: acceptance evidence

REAPER load/routing/project recall, actual audio devices, accepted click thresholds,
callback allocation/deadline measurements and clean-machine installation remain
unverified. These are validation gaps, not missing DSP source. Passing the Windows
five-second startup smoke does not establish audio-device operation. Before a public
beta claim, execute the spec's host/QA matrix and record measured criteria. Existing
builds are usable engineering artifacts; release packaging needs VST3/Standalone
bundles, install instructions and product/dependency license inventory. Vendored
skill licenses do not establish the application's distribution license.

### A03 — P1 reproduced: scene selection and host parameter diverge

A headless processor probe set Sample Clock + Immediate, requested B through
`requestVariation(1)`, then processed 256 samples. Result: actual active variation
was 1 (B), while APVTS `activeVariation` remained 0 (A). Processor.cpp:172–175 only
sets uiRequest; process publishes a separate activeVariation atomic at :435.
Editor/MIDI paths do not notify or update the host parameter.

Impact: the host's Variation control can disagree with the plugin buttons, and a
host request for the already-stale value may not generate a value-change request.
Next action: define requested versus active variation semantics, then synchronize
host-visible feedback safely outside the audio callback or expose them distinctly.
Acceptance: UI, MIDI, automation, cancel, preview and project reopen have a coherent
contract; no audio-thread host notification or unsafe feedback loop is introduced.

### A04 — P1 reproduced: conflicting MIDI assignments

The probe assigned note 60 to A and B; SceneBank retained both assignments and
`variationForNote(60)` returned A. SceneBank.cpp:129–136 clamps ranges without conflict
validation; MidiNoteMap.cpp:9–11 selects the first match. The editor offers no warning.

Next action: select and document a duplicate-note policy (reject, swap or visible
first-match warning). Acceptance: users cannot unknowingly disable a scene's MIDI
trigger; undo/state restore obey the policy and show conflicts from older states.
MIDI-learn and channel filtering are separate usability enhancements.

### A05 — P1 integration blocker: generator PR #9

Branch `feature/deterministic-variation-planner`, commit
`66a3ee056b798373e477d3629d63ed3e861a8829`, contains only an isolated settings planner
and test. At VariationPlanner.cpp:25 `auto rng` is inferred as unsigned long long
because of its ULL multiplier. On Linux GCC, uint64_t is unsigned long; calls to
`unit(uint64_t&)` / `next(uint64_t&)` cannot bind that reference. Direct C++20 compile
reproduced the error at lines 33–37. CI Linux core/plugin/sanitizer builds fail for
this reason, while Windows jobs pass. This does not affect merged main.

Next action: explicitly type PRNG state as uint64_t; rerun Linux/Windows/sanitizers.
Replace assert-only checks in VariationPlannerTests.cpp, which disappear under
Release/NDEBUG; a green Release executable currently does not execute those
assertions. After core verification, finish generator request/settings validation,
preview/apply/undo/state/UI integration and approved F01 contracts. Acceptance:
seed/style/candidate/protection reproducibility with enabled assertions in Release,
bounded valid results, stored-bank isolation and explicit user commit.

### A06 — P1 usability: operation automation and scene authoring

Per-operation controls are scene edits only; no stable automation parameters expose
their enable/depth/slice values. Copy/duplicate/reset-to-starter scene controls and
user fade-duration controls are also absent. Define automation versus discrete
scene-edit semantics before adding parameters. Acceptance: a DAW can automate the
promised continuous operation controls; IDs remain stable; state/undo interaction
is documented; scene copy is independent and undoable. Fixed-order operation
reordering would be a separate enhancement, not a current specification defect.

### A07 — P2 documentation and feedback

Commit genuine verified host screenshots and arrow overlays with a manifest;
provide a visible help entrypoint. Show requested/effective history-clamped slice
length, and clarify scene-only undo scope. Add keyboard/accessibility testing for
minimum and resized layouts. Acceptance: all important workflows have accurate
screenshots, arrows terminate in actual controls, and UI limitations are discoverable.

### A08 — P2/P3 deferred differentiators

Priority after core authoring and validation: generated candidates with safe preview,
scene morphing, downbeat/bass protection and compact performance controls. Audio
import/capture, multiband, gesture recording, audio triggers and CLAP add substantial
state/buffer/host contracts; implement only from reconciled specifications. This
ranking is an engineering recommendation, not a market-demand finding.

## Expansion package and branch status

| Item | Observed state | Delivered in main? |
|---|---|---|
| PR #6 expansion package | Open; `51ba7be9ecd145f2d92a875efd81454046441467`; status marker INCOMPLETE, all F01–F12 DRAFT, base/specification SHAs null | No |
| F01 variation generation | Planner core only on PR #9; Linux build broken; no user workflow | No |
| F02 scene morphing | Draft specification | No |
| F03 groove protection | Draft specification; per-operation settings protection in PR #9 is not groove protection | No |
| F04 performance mode | Draft specification; current scene buttons do not constitute its full contract | No |
| F05 phrase evolution | Draft specification | No |
| F06 polyrhythmic lanes | Draft specification; no event-lane model in main | No |
| F07 audio triggers | Draft specification | No |
| F08 variation history | Draft specification; current undo stack is not a candidate-history workflow | No |
| F09 audio import/capture | Draft specification | No |
| F10 multiband | Draft specification | No |
| F11 gesture recorder | Draft specification | No |
| F12 MIDI/CLAP/platform expansion | Draft specification; existing note input is only part of proposed scope | No |

The spec marker still names the pre-merge Claude target and lists timing integration
as unresolved, although that integration has now merged. Issue #5's old claim that
the marker is absent must be read by ref: it exists on the spec branch and remains
absent on main. Next spec action: pin current canonical baseline, reconcile exact
interfaces/tasks/tests and independently review readiness. The user's approval of
the feature direction does not make incomplete technical contracts complete.

## Executed checks and limits

- Compared current main to integration SHA across src, tests, CMakeLists.txt and
  workflows: no differences. Reran the matching eight Linux test executables:
  DSP, phrase clock, scene bank, serializer, undo/audition, scheduler, transport and
  processor/editor; all passed. This reruns existing binaries, not a fresh build.
- Compiled/linked a temporary processor probe against the verified shared-code
  library: confirmed A03 and A04 with outputs `actual=1 host_parameter=0` and
  `Duplicate MIDI notes retained=1 note60_resolves_to=0`. No product source changed.
- Directly compiled PR #9 planner + tests with GCC C++20; compiler rejected PRNG
  reference binding. [CI build failure](https://github.com/djshellshoxxx/GrooveMorph/actions/runs/38243004379)
  and [sanitizer build failure](https://github.com/djshellshoxxx/GrooveMorph/actions/runs/38243004122)
  match the reproduced error. Sanitizer **build** failure is not a detected sanitizer runtime fault.
- Verified merged runtime [Windows/Linux matrix](https://github.com/djshellshoxxx/GrooveMorph/actions/runs/38241019688),
  [sanitizers](https://github.com/djshellshoxxx/GrooveMorph/actions/runs/38241019653) and
  [Windows startup/artifact](https://github.com/djshellshoxxx/GrooveMorph/actions/runs/38241019648)
  succeeded at 7f2784a. The artifact is an engineering CI build, not a finished release.
- Source/UI/tests/help inventory found no hidden delivered implementations of the
  absent features. Empty returns in Scheduler/program-name APIs are intentional
  no-event/single-program behavior, not unfinished feature stubs.
- Did not perform new DAW/device/listening/accessibility/performance/installation
  tests. No completion percentage is supplied because proposal, deferred scope,
  branch-only infrastructure and verified user workflows have different meanings.
