# C05 — Automatic return

## Objective and integration

Launch a temporary scene/fill and return automatically after a musical duration,
without manually scheduling a second scene. Extend the existing Scheduler with
one return token and explicit precedence. No independent timer thread, free-running
wall clock, nested return stack or duplicate Scheduler. Depends on C04 launch phase
and C01 musical position. Providers src/core/control/ReturnPolicy.* and existing
Scheduler; processor applies launches/dry overlay, ReturnInspector displays status.

## Required controls and exact values

Per launch scene APVTS:
- Return enabled: false.
- Duration: .0625..64 meter beats, default4, step1/16. UI displays beats or bars;
  bar display is a unit conversion using meter at LAUNCH, not a second stored value.
- Target: Previous=0,A=1,B=2,C=3,D=4,Dry=5; default Previous.
- Return quantization: Immediate=0,Beat=1,Bar=2,Phrase=3; default Bar.
- Return phase: Preserve=0,Restart=1,AlignBeat=2; default Preserve.

Actions: Cancel return, Return now, Hold fill. Hold is a transient bool: on holds
expiry without changing configured duration; off computes expiry at current musical
position plus unelapsed hold duration (see below). Read-only destination, Remaining,
Armed/Held/Waiting/Returning status. No automated self-chaining or probability here.
`ReturnConfig { bool enabled; double durationBeats; ReturnTarget target;
ReturnQuantization quantization; PhasePolicy phase; }`;
`ReturnToken { uint64_t launchSequence,epoch; uint8_t previousScene,targetScene;
bool targetDry,held; double remainingQuarterNotes,lastPosition; ReturnConfig frozen; }`.
Timer is created at actual launch, not at request or queue time. C04 smoothing applies
on return using destination scene's current smoothing and explicit frozen return phase;
return target is captured at launch and never implicitly follows edit selection.

## State machine and timing

States Disabled -> Armed -> Held/WaitingForTiming -> Returning -> Disabled.
At an external scene launch, cancel any older token. If enabled and source scene
launch differs from target, arm one token, capture previously COMMITTED active scene
(preview is not committed active), target, meter beat size B and duration*B quarters.
If target equals launched scene, don't arm; show “Return target is current scene”.
Target Previous captures the prior actual scheduler scene, not stale APVTS requested
Variation value. Manual/MIDI/host launch while a fill is active cancels/replaces the
return; no nested stack. AutoReturn-origin launches NEVER arm another return, even
if destination has Return enabled. Prevent cycles A->B->A deterministically.

On each valid advancing musical segment decrement remaining by nonnegative
canonical delta. Tempo changes affect seconds-to-go, not chosen musical duration.
Frozen launch-meter duration remains constant in quarter notes through later meter
change, but a meter/epoch discontinuity cancels token under shared reset policy.
Explicit stopped transport freezes remaining. Strict missing PPQ enters Waiting;
resume with continuity only if epoch unchanged. Seeks, loops, state restore, sample
rate preparation or switching timing mode cancel token and publish cancellation reason.

At remaining<=0 schedule return at first configured boundary at or after expiry,
using canonical musical position and current grid. Immediate means expiry sample;
Beat/Bar/Phrase can extend the fill until that boundary. Do not quantize expiry back
into the past. Split current audio segment at expiry/boundary with sample-edge rule.
Example: 120 BPM/48k launch Q0, duration4beats -> expiry96000; Bar quantization returns
at96000. Launch Q1,duration2beats ->expiry72000 (Q3), Bar returns96000 (Q4).
“Remaining” distinguishes duration-to-expiry and waiting-to-boundary; both visible.

Hold freezes remaining at the frame the UI command is received. Always update lastPosition while Held, stopped or audition-frozen, so the held interval is not counted on resume. Release Hold keeps
that remaining musical duration; it does not restart the full fill. Return now
cancels old token and submits immediate captured-target return; Cancel return leaves
current scene active indefinitely. Neither global Cancel queued nor selecting
Manual later forces a timed return. Existing Cancel queued clears pending ordinary
launch; add Cancel return for its distinct lifecycle. If both external launch and
return occur at same frame, external launch wins, old return cancelled. Token
sequence/epoch validation prevents late worker/GUI commands cancelling a newer fill.

## Dry target and DSP chain

Dry is a transient return overlay that ramps effective master wet contribution to
zero, preserving intensity/dryWet/bypass host parameter values and stored scene.
Committed scheduler scene remains the launch scene; status says “Dry return; scene B
retained”. A deliberate external scene request, including the current active scene, clears
overlay with the normal smoothing (normal active-scene cancellation semantics are
preserved) and
runs its intended scene. Do not store Dry as a fifth scene, set bypass parameter,
edit scene depth or permanently overwrite host mix. Incoming dry signal/history
continue; existing host/internal bypass has final precedence. Dry override and timers
are not persisted. Cancel/restore resets overlay to configured project controls.

During audition, scene launch/return scheduling follows committed scheduler scene
but does not interrupt the audition audio; preview status must explain that return
is suspended AUDIBLY. To avoid hidden fill expiration, freeze return elapsed time
for the duration of audition; resume remaining on Revert/Apply, or cancel explicitly.
Applying a candidate never implicitly creates a return unless Play after apply launches
it. Editor close ends audition then resumes token; module has no GUI-lifetime dependency.

## GUI

Small return-arrow badge on A–D scene button when that scene's policy is enabled.
Clicking badge opens Return tab; ordinary scene button still launches. Inspector:
Enable, duration numeric + Beat/Bar display toggle, Target, Return quantization,
Return phase. Quick current-fill footer provides Hold, Return now and Cancel return
plus captured destination and two countdowns. Footer remains reachable when drawer
is closed; tab is the same docked/right or bottom/narrow inspector. No popup for
performing a fill. Countdown uses actual token, not mutable settings being edited.
Show requested policy versus armed frozen policy and explicit cancelled/held reason.

Host automation of duration/target/config affects FUTURE launches only. User can
change a future policy while current token remains unchanged. Hold/Cancel/Return now
are timestamped commands, not accidentally persistent trigger parameters. Optional
MIDI command bindings require dedicated mapping design outside this package; existing
note-on/off ReturnToPrevious policy remains separate. A note-off return cancels auto
token as a new external MIDI launch, preventing a second later surprise return.

## State, errors and bounds

Old v1: Off,4beats,Previous,Bar,Preserve; token absent. Save per-scene scalar config
through APVTS; do not save remaining/token/dry overlay. Undo settings does not execute
return. Invalid target/NaN duration recovers to defaults with report; never return to
an invalid index. One token + one pending normal launch only; last external launch
wins. No unbounded queue, callback heap work or OS clock. Long stopped periods do
not increment elapsed. No telemetry-only host notifications from callback.

## Tests and acceptance

C05-T01 exact96000 and expiry72000/bar96000 examples across irregular block/MIDI splits.
C05-T02 tempo120->60 midway changes sample duration but preserves musical remaining.
C05-T03 external launch and auto return same sample: external wins; note-off return
cancels token; auto-return destination with enabled return never chains. C05-T04
Previous captures actual active B despite stale requested parameter A. C05-T05
stop/hold/preview freeze, release resumes remaining; seek/loop/clock/sample-rate reset
cancels once with reason; missing PPQ waiting never invents elapsed time. C05-T06
Dry leaves all host/scene values unchanged, new launch clears overlay, bypass wins;
state restore contains no token. C05-T07 future policy automation cannot redirect an
armed token; Cancel queued versus Cancel return distinct. C05-T08 countdown/footer,
keyboard/focus/scene badge and preview status correct; host/audio RT acceptance gates.

## Implementation slices

ReturnPolicy provider/state machine fixtures; extend Scheduler single token and
request origin/priority; exact segment expiry/boundary mapping; dry-overlay DSP
adapter; APVTS config/state migration; ReturnInspector/footer/badges; MIDI/preview/
restore integration and maximum-load platform tests. See INTEGRATION-PLAN.md.
