# C04 — Transition phase

## Objective and chain role

Choose whether a destination variation follows the running phrase, restarts at
its beginning or snaps its local pattern phase to a meter beat. This is independent
of launch quantization: NextBar chooses WHEN, Phase chooses WHERE in the pattern.
Extend existing Scheduler's launch context and RhythmEngine phase/ramp input;
keep one owner of active/queued scene. No extra effect stage, second chain, full
capture or pitch-preserving stretch. Providers src/core/control/PhaseResolver.*
and existing Scheduler; processor supplies per-segment rhythm phase.

## Controls, config and authority

Per destination scene, stable APVTS parameters:
- Phase policy: Preserve=0, Restart=1, AlignBeat=2; default Preserve.
- Phase offset: -4..4 meter beats, default0, step1/16; add after policy selection.
- Transition smoothing: 3..500 ms, default3, step1ms; configures bounded read-voice
  crossfade and operation/gate ramps at that scene switch, not unrelated bypass fade.
Actions: Restart this scene (explicit normal launch to same scene with retrigger flag),
Reset phase offset, phase-preview diagram. Existing Transition selector remains
Immediate/Beat/Bar/Phrase/Manual; do not duplicate it here.
`PhaseConfig { PhasePolicy policy; float offsetBeats; float smoothingMs; }`;
`PhaseState { double anchorQuarterNotes; double localPhaseAtAnchor;
uint64_t epoch; uint8_t scene; bool valid; }`;
`resolveLaunch(config,livePosition,grid)->PhaseState` and
`localPhase(state,currentPosition,grid)->double` are pure, finite, bounded functions.
Snapshot contains config at REQUEST time; editing knobs later affects future
launches, not a queued request or current phase. Manual explicit Restart can relaunch
active scene; preserve old requestVariation(active) cancel semantics for normal buttons.

## Exact phase rules

Let Q=current canonical musical quarters relative to global grid origin, P=phrase
quarters, B=quarters/meter beat, O=offsetBeats*B. At actual switch frame:
- Preserve: local phase positiveModulo(Q+O,P).
- Restart: local phase positiveModulo(O,P).
- AlignBeat: local phase positiveModulo(ceil(Q/B-1e-12)*B+O,P).

AlignBeat snaps pattern phase forward to the next meter beat **without delaying
launch**; UI text is “Snap pattern to next beat”. Quantization still controls launch
time. At exact boundary ceil tolerance holds current beat. Store anchor/live phase
and advance by canonical musical delta; do not derive local phase from raw samples
at a changed tempo. Phase offset fractional values are rounded to UI grid for direct
edits only; automation remains finite clamped value observed at callback.

All C01 event/window positions use local phase. Cadence Every/Offset uses absolute
global phrase index, so Restart does not circumvent every-four-phrases policy.
Generator source event coordinates remain unchanged. Activity masks apply in local
pattern coordinates. Changing scene phase does not modify persistent event onset,
source buffer write position or host transport. Existing history remains causal;
backwards source windows still clamp as before. Do not read future input merely
because local pattern phase moves forward.

On actual seek/loop/meter/clock epoch change, cancel phase anchor and reset to
Preserve at the new global location; preserve config for future launches. Clear
history only for real transport discontinuity according to existing policy. While
stopped, Immediate/Manual launches can select/anchor a scene but rhythmic processing
remains dry; Restart's anchor starts advancing when playback resumes. Strict missing
PPQ postpones quantized launch; same-frame command priority follows shared rules.

## Smoothing and audio integration

At launch event offset n, processor renders pre-n audio with previous scene context,
then installs destination phase/config and continues through the same engine.
Each active history voice retains its outgoing read trajectory in existing bounded
retiring taps and starts the new trajectory; gate and operation amounts slew using
chosen smoothing. No ring allocation/copy, no double chain rendering, no future
lookahead. Smoothing is sample-count based from milliseconds/current sample rate,
not callback length. Existing two-retiring-tap cap remains; on a third transition
within fade, retire the oldest tap deterministically and increment diagnostic count.

Phase timing commits exactly at n; audio smooths over the following duration.
UI distinguishes transition point from smoothing completion. At most two retiring
read taps per operation, full stage order unchanged. Same-phase/same-settings launch
must not introduce a gain bump. Parameter intensity/dryWet and host bypass retain
their own existing ramps. A fade of whole wet chains/equal-power mix is not promised
by this implementation; operation-specific smoothing must pass defined fixtures.

## GUI and performance use

Transition tab beside existing Transition selector shortcut. Main row contains
Phase policy and miniature source/destination phrase marker. Detail contains Offset
(beats) and Smoothing(ms). Display queued request's frozen policy separately from
current controls. “Restart current” asks no popup confirmation and acts through the
normal timestamped request path; tooltip explains transport remains unchanged.
At narrow widths this is the shared bottom tray, not a floating modal window.
Provide accessible numeric text, keyboard step, reset and status “B starts at beat1”.
A queued return from C05 uses its separately captured return-phase policy; user
can inspect it in Return tab. Do not make changing Phase silently enable return.

## Persistence and error recovery

APVTS owns per-scene config, old state defaults Preserve/0/3ms. PhaseState/retiring
taps are transient and not saved. A loaded project returns to host-preserved phase,
not a supposedly continuous old live-performance cursor. Unsupported enum/nonfinite
offset/smoothing is recovered to defaults with status; no cast from NaN to integer.
Undo config edit updates host safely off-thread but cannot retrigger audio. Changing
phase config is not a scene-content mutation and is not frozen by C03 data locks.

## Tests and acceptance

C04-T01: 4/4,4 bars, switch at global beat6.25 -> Preserve6.25, Restart0,
AlignBeat7; offset-.25 ->6/15.75/6.75. At exact7 -> AlignBeat7.
C04-T02: 7/8 B=.5 quarters; phase snap uses meter beat, not quarter-note rounding.
C04-T03: same input/request stream across blocks1/64/257/1024 gives same switch
sample/local phase and tolerance-documented output; fractional PPQ edge included.
C04-T04: tempo map changes preserve anchor delta; seek/loop/meter/mode epoch resets
history/phase according to policy without corrupting scene data. C04-T05: 3/50/500ms
ramps at44.1/48/96/192k have correct sample lengths; same-settings preserved-phase
switch nulls; sine/impulse/drum fixture click measurements meet agreed threshold.
C04-T06: three rapid transitions bounded retiring voices; no allocations, extra
chains or stale source history. C04-T07: queued policy remains frozen when UI changed;
active-scene normal click cancels queue, explicit Restart retriggers; saved v1 output
and state remain compatible. C04-T08: UI marker reflects actual preview/queued/active
phase and destination scene at narrow/wide sizes; host save/reopen tests.

## Build plan

Pure phase provider/fixtures; Scheduler typed-context overload + old API delegation;
processor per-segment phase adapter; bounded voice/ramp configuration; stable params
and TransitionInspector; whole-plugin audio/automation/state/host QA. Sample offset
mapping remains in canonical clock, not a separate phase scheduler.
