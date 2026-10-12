# GROOVEMORPH — COMPLETE IMPLEMENTATION, INTEGRATION, AUDIT, QA, AND NATIVE RELEASE

## Mission and authorization

Repository: https://github.com/djshellshoxxx/GrooveMorph

You are the implementation lead, real-time DSP engineer, JUCE/C++ engineer,
integration coordinator, and release engineer. Complete GrooveMorph's full
approved release scope from its specifications. Implement missing features,
repair verified defects, integrate existing work, audit the final application,
optimize measured bottlenecks, test it, and produce Windows and Linux releases.

This task explicitly authorizes application implementation, commits, pushes,
reviewed merges, CI execution, and verified GitHub prerelease publication. Earlier
specification-only instructions do not restrict this implementation task.
Do not stop after a plan, a scaffold, a core-only test, or a successful compilation.
Continue until the approved deliverables are complete or a concrete external
blocker prevents further progress. Finish independent work while a blocker remains.
Do not invent completed host tests, measurements, permissions, or tools.

## 1. Use the existing skills folder

The repository already contains `skills/` at its root. Do not reinstall or recreate it.
Read `AGENTS.md`, `skills/README.md`, and
`skills/superpowers/using-superpowers/SKILL.md`. Then load relevant skills on demand.

The bundle contains the full 15-skill Superpowers directory and these ten audio skills:

- `audio-dsp-review`
- `audio-numerics-review`
- `audio-performance-debug`
- `audio-artifacts-debug`
- `audio-signal-flow-explainer`
- `audio-math-explainer`
- `dsp-algorithm-guide`
- `juce-guide`
- `juce-review`
- `vst3-review`

Use `skills/caveman/SKILL.md` for concise status and inter-agent messages. Preserve
exact commands, paths, errors, evidence, numbers, and negations. Keep persisted
source, comments, contracts, documentation, commits, and release notes clear and complete.
`ultimate-protocol-simulator` is optional only for explicitly selected JSON-only
automation; never combine it with Caveman prose. Antigravity protocol skills are excluded.

Resolve `superpowers:<name>` to `skills/superpowers/<name>/SKILL.md` if no plugin
is registered. Read files directly when native skill discovery is unavailable.
Skills do not create tools or workers. Check actual host capabilities. Verify
version-sensitive guidance against the pinned JUCE source and official docs;
trace actual callback threads instead of assuming all state callbacks use the UI thread.

## 2. Verify current state before assigning work

The following observations are a review baseline, not permanently current facts.
Fetch current refs, PRs, CI, and releases before using them:

- Reviewed `main`: `4b8ed2e`, an early M0 scaffold.
- `codex/m0-validation`: `a541b636e6407580317af8c77459350b81a73626`.
- `claude/awesome-keller-jx2ck0`: `dfcabda030f7e1432f4b51baad471b26ddc91f0c`.
- The reviewed Claude branch contains the Codex branch as an ancestor. Do not
  cherry-pick or reimplement the same M0 work twice.
- Its CI run https://github.com/djshellshoxxx/GrooveMorph/actions/runs/38148457507
  passed all four Windows/Linux core and full plugin/standalone jobs. This does
  not establish REAPER compatibility, audio quality, or complete functionality.
- At review there were no open PRs and no GitHub releases. Branch ownership may
  still be active; absence of a PR does not make a branch abandoned.

Inspect/review the newer branch, rerun relevant checks at its exact head, and
integrate it with the current skills-bearing `main` through a small PR. Preserve
concurrent additions to `AGENTS.md`, README, specs, CI, and skills. Verify ancestry
and semantic conflict resolution; do not force-push another session.

The old main scaffold references absent `src/StandaloneMain.cpp`, duplicates
`createEditor()` and editor definitions, and has no proper core/test architecture.
The newer branch fixes this build structure; use it rather than patching the obsolete
shell again. Inspect `.gitignore`, which contained C++ header text at review, and
replace invalid ignore content with appropriate build/cache/artifact exclusions.

The newer branch supplies `docs/SPECIFICATION.md`, `docs/SPEC_INDEX.md`,
`docs/TRACEABILITY.md`, `src/core/PhraseClock.*`, `TransportResolver.*`,
`OperationParams.h`, APVTS parameters, processor tests, and native CI. Keep those.
Update stale progress notes from verified evidence. No current branch is a
completed GrooveMorph implementation: the processor is still a gain/timing spike.

## 3. Establish the source of truth and completion contract

Read the integrated specification, index, traceability, `RESEARCH_NOTES.md`,
`GETTING_STARTED.md`, CMake files, workflow, core modules, processor, editor, and tests.
Create or reuse one task ledger with requirement ID, owner, files, dependencies,
base/head commits, branch/PR, acceptance checks, status, blockers, and next action.

Complete all mandatory requirements in the approved scope, with traceability from
requirement to module, UI/adapter wiring, tests, and actual acceptance evidence.
Treat GM-FR-001 through GM-FR-012 and applicable GM-QR requirements as essential.
Implement additional approved requirements where the current specification makes
them mandatory. Do not treat optional recommendations or explicitly deferred
features as prerequisites for this release.

Current approved stack: C++20, CMake, pinned JUCE; VST3 and JUCE Standalone;
Windows x64 and Linux x86_64; REAPER as the initial reference host. JUCE 8.0.7 is
the reviewed dependency. Do not upgrade the framework merely because a newer
version exists. Resolve any necessary upgrade through a focused compatibility decision.

CLAP, full phrase capture, pitch-preserving stretching, multiband processing,
cloud services, audio-file acquisition, and a broad unrelated effects suite are
deferred unless the owner changes scope. Rate-changing effects may change pitch;
document the actual behavior. Complete the approved product without expanding scope.

Where specifications have material gaps, do focused primary-source research and
write developer-ready contracts before implementation. Record ranges, units,
ownership, lifecycle, failure behavior, algorithms, edge policies, tests, and
acceptance tolerances. Make conservative routine engineering decisions and record
them; do not repeatedly ask approval for already authorized work. Do not silently
lower provisional quality targets to obtain a passing result.

## 4. Cost-efficient teamwork and early integration

Use as many useful agents or cloud sessions as the environment actually supports.
Every participant, including the coordinator, must use the cheapest available
model capable of meeting its task's quality bar. Verify actual availability and
costs where exposed. Do not assume expensive models are required for difficult
tasks; first consider decomposition and strong tests. Escalate based on observed
failure or uncertainty, considering retries and coordination in total cost.

Every worker needs a unique ID, bounded deliverable, contract, owned files,
isolated branch/worktree, acceptance tests, and integration target. One owner per
task. Check active work before claiming it. The coordinator owns shared contracts
and integration order; workers cannot independently redesign public interfaces.
Do not launch duplicate implementations or idle workers.

Merge each reviewed, passing unit promptly, notify affected workers, and refresh
dependent branches. Preserve active work and valuable unmerged commits. Record
blocked work with a handoff; never leave unexplained orphan branches. Delete a
branch only after its work is safely preserved and its ownership is resolved.

Use focused reads, bounded logs, cached findings, reusable fixtures, and concise
handoffs. Reserve budget for integration, audits, testing, packaging, and release.
Near budget exhaustion, push verified work, integrate safe units, and save exact
continuation state. Token savings never justify missing wiring or reduced checks.

## 5. Finish M0 and implement the foundation

Reconcile M0 build evidence with stale documentation. Run native Windows/Linux
builds, core/processor tests, VST3 validation, and available standalone/host smoke
checks. If REAPER or an audio device is unavailable, record the exact missing
check and continue independent work whose correctness does not depend on that
unverified assumption. Keep the M0 host gate pending; do not claim it passed.

Inspect and address these known M0 gaps before relying on the skeleton:

- `masterIntensity` currently acts as output gain: zero produces silence. The
  final effect's zero intensity must produce the defined dry path. Preserve the
  existing ID or provide explicit state/semantic migration and documentation.
- Timing currently uses sample position relative to sample zero. Implement PPQ,
  last-bar origin, tempo/meter changes, seeks, loops, and legitimate negative
  pre-roll according to host contracts before musical scheduling depends on it.
- The resolved play-state is not yet consumed. Define stopped/armed behavior and
  explicitly selected standalone/manual free-run. Do not start playback silently.
- Cache raw APVTS parameter pointers outside processing; audit the complete
  callback call graph and status publication for bounded, race-free behavior.
- Gain smoothing is currently block-length-dependent. Define smoothing in samples
  or time, test block-size invariance, and preserve sample-offset timing where
  the chosen JUCE/format API actually exposes it.
- Current malformed-state tests expect ignored input, while specification recovery
  text proposes safe defaults. Resolve this contract explicitly, preserve atomic
  restore and existing valid state appropriately, and test the chosen behavior.

## 6. Implement M1–M4 as complete, reusable vertical slices

Implement the specified portable modules: CoreModel, PhraseClock/transport,
SceneScheduler, AudioHistory, TransformEngine, TransitionMixer, StateCodec, and
CommandBridge. JUCE and UI adapters must delegate to these services. Keep module
interfaces small, typed, documented, testable, and independently reusable.

Required behavior:

1. Phrase length, origin, subdivision, sync source, validated timing, and the same
   boundaries in scheduler and display. Handle fractional tempos and long sessions.
2. Four named independent variations with stable identities and editable events.
   Editing, duplication, audition, and recall must not mutate unrelated variations.
3. Repeat/stutter, bounded reverse slice, gate, half-time feel, and tape-stop/rate
   ramp. Specify source-window age, duration, interpolation, wrap handling,
   readiness, warm-up, boundaries, click prevention, and maximum history capacity.
4. Immediate, next beat, next bar, next phrase, and manual/user-triggered switching;
   visible queued state/countdown; deterministic request priority and overflow policy.
5. Master intensity, per-operation enable/depth, aligned dry/wet, host/internal
   bypass, reset, latency, tail, and safe behavior on insufficient history.
6. Manual selection, stable host automation, and timestamped MIDI note triggers;
   explicit mappings, note-off semantics, simultaneous-event priority, and fallback.
7. Complete versioned recall of parameters, variation names/events, grid, transition
   settings, and MIDI mappings. Validate capped inputs into temporary models and
   publish atomically. Test migration, corruption, future versions, and unload.
8. Audition/compare and bounded undo/redo with exact model restoration. Keep
   temporary audition state separate from persisted choices and host automation.

The audio callback must allocate/deallocate nothing, wait on no lock, perform no
disk/network/logging/UI operation, and execute only bounded work. Preallocate and
initialize history/scratch; define maximum event counts and queue capacity. Prove
queue ownership and snapshot lifetime, including reclamation outside the callback.
Do not assume atomic shared pointers or small objects are lock-free.

Never claim arbitrary reverse/time effects are zero latency. Measure the actual
algorithmic delay, report it to the host, and align dry/wet and bypass. If a
bounded operation violates accepted latency, revise its contract and document
the tradeoff before release rather than concealing it.

## 7. Complete M5: all UI and standalone wiring

Implement the specified status strip, phrase canvas/lane editor, four-variation
strip, intensity/transition/trigger/dry-wet controls, detail inspector, and
undo/compare flow. Show active and queued variation, sync source, missing host
fields, readiness, and transition timing using text/shape as well as color.

Every visible control must change the intended model/DSP state, support valid
host automation gestures where applicable, persist correctly, and restore its
display after host changes. Test empty/default/invalid states, rapid edits,
resizing, keyboard focus, 100–200% scaling, and multiple editors/instances.
Do not leave placeholder controls or panels disconnected from processing.

Standalone must use the same processor, provide usable audio-device and
input/output configuration, manual transport/tempo, and the required test/audition
path. Verify device failure/reopen, sample-rate and block-size changes, and actual
audio flow. Do not turn the validation application into an unrelated DAW.
Transient suggestions remain optional unless current approved scope requires them;
if implemented, use bounded worker snapshots, confidence, manual correction,
generation IDs, cancellation, and no automatic uncertain grid replacement.

## 8. Audit, bug hunt, and optimization

After feature integration, perform separate deliberate passes over the final code:

- Completeness: every mandatory requirement, module, adapter, UI control, parameter,
  persistence field, build target, and acceptance check is wired end to end.
- Serious bugs: bounds/lifetimes, uninitialized history, stale scenes, races,
  queue overflow, counter overflow, invalid host context, state corruption,
  cancellation/shutdown, silent fallback, and wrong timing/latency.
- Audio quality: clicks, DC, denormals, NaN/Inf, aliasing, phase/crossfade behavior,
  output bounds, channel independence, repeat/reverse/rate continuity, dry null,
  bypass, transport discontinuities, and determinism.
- Build readiness: CMake target/source registration, headers/ODR, JUCE config,
  compiler warnings, platform libraries, resources, packaging paths, architecture,
  metadata, licenses, and clean-run dependencies.
- Optimization: benchmark first, fix measured hotspots, rerun correctness tests,
  then compare before/after measurements under identical conditions.

For every confirmed defect, record evidence, add a reproducer, fix the root cause,
run targeted and affected regression tests, and integrate the passing repair.
Keep hypotheses distinct from confirmed findings. Do not stop after one audit scan.
Avoid speculative frameworks or refactoring unrelated working code.

## 9. Tests and native builds

The integrated baseline supports:

```sh
cmake -S . -B build-core -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release
ctest --test-dir build-core --build-config Release --output-on-failure

cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

Run these in native Windows and Linux environments with documented prerequisites;
adapt generator/configuration flags to the actual toolchain. Use existing CI and
improve it with artifact uploads, DSP tests, validators, sanitizers where supported,
and frozen/release-directory smoke checks. Never treat Linux compilation as proof
of Windows success. Validate VST3 and Standalone targets separately.

Cover impulse, silence, DC, sine/sweep, noise, ramp, drum/bass fixtures; history
wrap/warm-up; malformed state/events; seeks/loops; rapid automation/scene changes;
mono/stereo; zero and variable blocks; 44.1/48/88.2/96 kHz; 32–2048 frames where
supported; extreme valid tempo/meter; resets; and concurrent UI/state activity.
Document narrower tested ranges rather than claiming untested support.

Run REAPER Windows/Linux scan/load, MIDI routing, automation, bypass/PDC,
project save/reopen, transport changes, offline rendering, repeated instances,
and standalone audio-device tests. Use truthful unavailable-check labels when
native desktop tools are absent. Mocks do not establish host compatibility.

Measure the specification's provisional CPU, memory, click, timing, and latency
targets with machine/compiler/host settings and reproducible workloads. Include
the specified 24-hour stress requirement before calling full acceptance complete.
Arrange durable long-running tests where tools support them; do not claim a short
smoke test satisfies that gate. Record blocked checks with exact continuation steps.

## 10. Package, publish, and report completion

Produce native Windows x64 and Linux x86_64 VST3 and Standalone artifacts from
the final integrated commit. Archive complete required resources/runtime contents,
generate SHA-256 checksums and dependency/license inventories, and test extracted
packages. Provide installation, launch, audio setup, usage, uninstall/upgrade,
rollback, limitations, and troubleshooting documentation. Confirm the applicable
JUCE/product distribution terms; do not invent a product license or paid signature.

Publish a new GitHub prerelease only when its declared acceptance gates pass;
attach both platforms' intended artifacts, checksums, notices, and accurate release
notes. Never overwrite unrelated releases. Verify artifact URLs, sizes, checksums,
version metadata, and exact source commit after publication. If mandatory checks
remain unavailable, retain verified build artifacts and clearly report the blocker;
do not label the application fully complete or validated.

Final report: integrated commit and PRs; completed requirements; fixed defects;
actual tests/validators/host results; measured performance/latency; Windows/Linux
artifact and release links; checksums; unresolved gates; remaining branches/owners;
and credit usage if visible. Keep durable continuation records and no orphaned work.

**Start now:** load the existing skills, verify current branches and ownership,
integrate the newer M0 work, establish traceability, and complete the approved
product through implementation, verification, audits, native packaging, and release.
