# Integration, task ownership, migration and verification plan

## Scope and existing-feature mapping

C01 supports the missing FR-001/002 authoring contract and later F06, without
independent polyrhythmic clocks. C02 elaborates F01 distance/generation controls;
C03 is a mutation-permission provider, not F03 audio groove protection. C04/C05
supply transition/performance behavior usable by F02/F04/F05. C06 provides candidate
activity constraints. Do not create six rival scene models or mark full F01–F12
implemented from this package. Fix A03 Variation feedback and A04 MIDI conflicts
in separate regression-tested changes before final end-to-end acceptance.

## Provider ownership and modules

| Owner scope | Files | Consumer boundary |
|---|---|---|
| Shared types/state | core/control/ControlTypes.h; core/scene/SceneBank.*, SceneSerializer.*; PARAMETERS.json | Validated v2 scene + scalar adapter snapshot |
| C01 | core/control/PhrasePlacement.*; tests/core/PhrasePlacementTests.cpp | Prepared events/phase targets |
| C03 | core/control/ChangePermissions.*; tests/core/ChangePermissionsTests.cpp | Mutation permission/report |
| C06 | core/control/ActivityLimiter.*; tests/core/ActivityLimiterTests.cpp | Compiled retained mask/report |
| C02 | core/control/VariationDistance.*; tests/core/VariationDistanceTests.cpp | Cancellable materialized candidate |
| C04 | core/control/PhaseResolver.*; Scheduler.*; PhaseResolverTests.cpp | One canonical typed launch/phase state |
| C05 | core/control/ReturnPolicy.*; Scheduler.*; ReturnPolicyTests.cpp | One return token and dry-overlay events |
| Integration owner | PluginProcessor.*, parameter cache/state/model dispatcher, RhythmEngine.* | Timestamped command/config/operation target segments |
| UI owner | UI control components + PluginEditor.*, tests/PluginProcessorTests.cpp | Six inspector tabs, status, accessibility and host gestures |

New paths are under src/ unless tests/ indicated. Only one integration owner edits
Scheduler/processor/state/public manifests at a time. Pure provider tests can be
parallel once shared interfaces commit; no parallel edits to common files. Record
base SHA, files, dependencies, tests and PR in ownership notes; integrate small passing
units promptly. Current user request is specification work, not implementation.

## Proposed dependency order and thin slices

1. S00 canonical provider/type/parameter-state contract review; review A03/A04 fixes.
2. S01 v2 serializer/legacy null fixtures and immutable snapshot adapter (≤5 files).
3. S02 C01 pure musical validator/window/event compiler and portable fixtures.
4. S03 C01 single-engine masks + grid/placement adapter and audio fixtures.
5. S04 PlacementInspector/canvas and transaction tests.
6. S05 C03 validator/permissions/state fixtures; then PermissionsInspector.
7. S06 C06 analyzer/pruning provider; compiled-mask pending revisions; ActivityInspector.
8. S07 repair PR #9 Linux RNG/Release tests; reconcile C02 draw engine/proposals/metrics.
9. S08 C02 permission/activity composition, cancellation/stale results; VariationInspector.
10. S09 C04 pure phase resolver; typed Scheduler extension; engine ramps; TransitionInspector.
11. S10 C05 pure return state machine; scheduler token, exact expiry and dry overlay.
12. S11 ReturnInspector/footer and MIDI/preview/automation/restore integration.
13. S12 whole-plugin regression/RT/host/GUI/packaging checks; screenshots/help/traceability.

Each numbered provider/UI slice is separately reviewable; split further if >5 files
or >~300 meaningful lines. Provider tasks precede consumers; no UI controls bound to
placeholder no-op processors. Tests precede behavioral changes. Existing tests kept.

## APVTS, state and model integration details

Instantiate fixed parameter manifest once in createParameterLayout, append after
old IDs. Cache pointers in constructor. Scalar adapters assemble four scene configs
from raw values; observe revisions, validate finite ranges and trigger off-thread
model preparation using atomic dirty flags. Parameter listeners may run on processing
threads: they may only set bounded atomics, never allocate or touch editor/state trees.
UI attachments bind a fixed scene's IDs; switching edit target destroys/recreates
attachments off-thread while listeners are suspended to avoid cross-scene writes.

Prepared SceneSnapshotV2 is a distinct trivially-copyable type composed of existing
settings + four bounded event patterns + configs + revisions/status. Preallocate
active/shared scratch and verify maximum envelope/memory sizes. Audio snapshot swap
at callback boundary; events/launches split later within the callback. UI never writes
scheduler/voices. GUI reads fixed status and formats strings itself. Save/restore
forms one coherent scene/parameter transaction off-thread; specify rollback and
recovery fixtures rather than applying half an invalid v2 project.

New RhythmTiming/processTargets input supplies operation amounts, slices and five
local beat phases. Legacy process delegates with old shared timing; C01 windows
and C06 masks never add an output audio gate. Each explicit event resets its lane
cycle at onset; preservation anchors are persisted only for explicit continuous
splits. Test bounded source-read transitions at those discontinuities.

No source mutation from planning until Apply. Off-thread generation publishes a
revision-tagged candidate; UI/controller owns cancellation and dispatch. Fixed draw
protocol for C02: mix64(x) performs x^=x>>30; x*=0xbf58476d1ce4e5b9; x^=x>>27;
x*=0x94d049bb133111eb; return x^(x>>31), modulo2^64. Start x=seed; fold fields
candidateIndex,attempt,operation,eventID,fieldTag by x=mix64(x ^ (field+0x9e3779b97f4a7c15)).
Unit draw=top24bits/16777216. Field tags1=choose,2=depth,3=onset,4=duration,5=slice,
6=enable,7=topology. Keep IDs/order independent; candidate algorithm version1 owns
these constants. Golden fixtures must pin platform results before shipping.

## Commands and code conventions

```sh
cmake -S . -B build-core -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release --parallel 2
ctest --test-dir build-core --build-config Release --output-on-failure
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build --build-config Release --output-on-failure
```

Windows full configure: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`.
Linux dependencies as GETTING_STARTED.md. Sanitizers use existing
.github/workflows/portable-core-sanitizers.yml. CMake already auto-includes
src/core/*.cpp and tests/core/*Tests.cpp; add no duplicate library/scheduler target.
Use repository C++20 naming/style, explicit fixed-width state types, constexpr
bounds, enum validation and noexcept callback/provider functions. Tests use fail
counters/exit status, not assertions disabled in Release. No new dependency is needed.
Never weaken tests, write vendor source, reuse old state IDs, allocate in process,
or call host/UI APIs from processing to make synchronization appear correct.

## Cross-feature acceptance and numeric fixtures

- Integrated musical example: 4-bar4/4, LastBar Every4 Offset3; Distance.25;
  first beat locked; cap2/coverage.25; launch B NextBar Preserve; Return after4beats
  to Previous NextBar. Verify activity only on intended phrase/bar, permission data
  unchanged, candidate Apply one undo, active/requested status coherent, return once.
- Run one-sample,64,257,512,1024 and oversized buffers; 44.1/48/96/192k; 3/8,7/8,4/4;
  tempo97/120/137/173; forward/back seek, loop, preroll, missing PPQ/bar/sample fields.
- Same event stream must have sample-boundary agreement within one sample; output
  equivalent across partitions within documented float tolerance. Legacy migration
  null tolerance ≤1e-6 full-scale per sample, unless an existing independently fixed
  defect is explicitly excluded with fixture/reference.
- Smoothing fixture target follows existing proposed GM-QR-004: unintended transition
  residual ≤-60 dBFS (0.001 full-scale amplitude), with separate intentional Gate
  discontinuities and original waveform residual defined in fixture; no blanket
  peak-step assertion on arbitrary source audio. Record measured listening exceptions.
- Bound max256events,8regions,5operation lanes,64commands; worst-case callback
  allocation/deallocation count0; no lock wait/I/O. Proposed max added control-snapshot
  memory <2MiB and total plugin owned memory <64MiB at192k/stereo/maxBlock8192;
  report sizeof/layout/history accounting rather than trusting an estimate.
- CPU target remains original provisional mean<5%,peak<10% of one measured desktop
  core at48k/128; document processor,compiler,OS,host and worst scene. A failed target
  requires an explicit engineering decision, not falsely claiming it passed.
- REAPER Windows/Linux: VST3 scan, MIDI, automation, freeze/offline, restore, PDC,
  bypass, mono/stereo, repeated instances, sample-rate change; Standalone device I/O.
- GUI at760×690,820×720,1120×720 and scaled DPI; keyboard/screen reader; six tabs,
  effective limits, queued frozen settings, preview/current return statuses.
- Invalid state/count/IDs/enum/NaN/Inf, stale async generation, queue overflow and
  superseded return commands leave safe current audio/state with visible recovery.

## Definition of implementation completion

Every feature requirement maps to its provider, adapter, widget, state and test;
all shared gates and feature fixtures pass at exact integrated SHA. Remaining
unavailable hardware/host tests are recorded as unverified, not closed. Screenshots
must come from actual verified UI with arrows; no source diagram labeled screenshot.
Specs/parameter/state/test/help indices updated. No production-ready claim based
only on successful compilation or a standalone process remaining alive.
