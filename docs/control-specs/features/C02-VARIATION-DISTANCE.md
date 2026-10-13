# C02 — Variation distance

## Purpose and place in the plugin

Generate controlled alternatives from a stored or audition source, with separate
rhythm and depth limits. This is a design-time planner off the audio thread, not
an effect-intensity control or continuous random DSP. It integrates existing
SceneBank/UndoStack/AuditionSession, consumes C01 event patterns, respects C03
permissions and passes C06 activity constraints. Source: src/core/control/VariationDistance.*;
reconcile useful PR #9 VariationPlanner into this provider rather than duplicate it.
PR #9 must first explicitly type PRNG state uint64_t and replace Release-disabled
assertions. No C02 user workflow exists in the baseline.

## Controls and actions

Global APVTS: Distance [0,1]/0.25, Rhythm amount [0,1]/0.5, Depth amount [0,1]/0.5,
Style Conservative=0/Creative=1/Experimental=2 default Conservative. Distance sets
a maximum allowed change, not a requested wet level or guaranteed audible score.
Scene state: exact uint64 seed (default existing bank seed), uint32 candidateIndex
(default0). Seed uses decimal/hex text entry, Copy and Reroll; never store uint64
as a float host parameter. Buttons Generate, Previous candidate, Next candidate,
Preview, Hear stored/edited, Apply, Revert, Use stored as source. Candidate index
increment can wrap modulo2^32; show index so results can be reproduced.

`DistanceConfig { float distance,rhythmAmount,depthAmount; Style style; }`;
`CandidateRequest { sourceScene,sourceRevision,DistanceConfig,PermissionConfig,
ActivityConfig,uint64_t seed,uint32_t candidateIndex }`; result contains prepared
candidate, effective/achieved scores, touched fields, bounded conflict report and
status Valid/NoChange/Conflict/Invalid/Stale. Inputs are snapshots, not live model
references. Source pinned when generating; edits change revision and mark stale.
Max32 candidate attempts, max256 events; result finite/bounded, no silent capacity cut.

## Algorithm and exact limits

Use SplitMix64 with explicit uint64_t intermediates; unsigned overflow intentional.
Counter-derived draw seed = hash64(seed, candidateIndex, attempt, stable operation,
eventID, fieldTag), using a documented fixed SplitMix mixing sequence in provider
fixtures. Never consume a sequential draw only when a field is unlocked. Locking
one item must leave random proposals for unrelated fields unchanged. Domain tags
are versioned constants; schema records plannerAlgorithmVersion=1.

Let d=Distance, r=d*RhythmAmount, a=d*DepthAmount. d=0 returns a semantic exact source
copy, consumes no source edits and preserves all IDs. Every unlocked depth delta
is bounded by a and clipped [0,1]. Operation/event Enable toggle probability is
style base probability (0.1/0.3/0.6) ×r. Timing displacement is rounded to grid,
absolute displacement ≤floor(r*P/2) ticks; cyclic shortest path is used. Duration
change ≤floor(r*P/2), constrained [1,P]. Slice proposals select allowed grid-friendly
values {1/16,1/8,1/4,1/2,1,2,4} meter beats with normalized log2 slice displacement
≤r. Event creation/deletion allowed only when topology unlocked, with maximum
floor(r*max(1,sourceCount)) topology edits per attempt, and total count≤256.
At sourceCount0, require Create from empty explicit action; never silently invent
an event from an empty/dry scene. LegacyUniform can vary its five settings; timing/
topology event generation requires explicit conversion, with explanatory status.

Style sets proposal probability/search weighting, never bypasses d/r/a limits.
Same-operation overlap makes an attempt invalid; don't relocate protected events
to repair it. C03 validates proposal; C06 prunes unlocked events if allowed; then
revalidate permissions and total topology-edit limit (including pruning deletions).
If activity limits force more edits than the distance budget, Conflict rather than
silently exceed d. Source already violating enabled limits can therefore be unsatisfiable.

Achieved Rhythm display = mean of normalized timing/duration/slice/enable/topology
differences over the union of event IDs and five settings; missing/added IDs count1
for their topology component. Depth display = mean absolute depth difference for
matched IDs/settings; missing IDs have topology cost, not artificial depth0.
These are explicit metadata measures, not perceptual/audio similarity. Evaluate
all valid attempts; choose highest achieved weighted score ≤configured field limits,
break ties attempt index. A lower distance need not yield a strict subset after
constraint solving; never promise monotonic audible complexity. NoChange is valid
when all fields locked or distance too small after grid snap; explain why.

Only current and previous candidate snapshots retained, not full F08 history browser.
For Previous/Next, deterministic regenerate by index from pinned source; expensive
work runs worker/controller thread and cancellation is checked between attempts.
Latest request wins; a sourceRevision mismatch prevents publication or Apply.
No generator work, random-device call, hash string construction or allocation in DSP.

## GUI and VST integration

Variation tab in common inspector. One main Distance slider, secondary Rhythm/Depth
sliders, style selector; seed/index expandable Advanced group. Always show Source
scene, target scene, permissions summary, activity summary and changed-field count.
Preview mini-map overlays candidate against stored lanes; changed fields use badges.
Generate does not modify stored scene or host transport. Preview delegates current
AuditionSession and uses actual incoming audio. Apply writes candidate to selected
scene as one undo transaction; it does not auto-launch that scene unless user checks
separate Play after apply option (editor preference defaultfalse), which submits the
normal Scheduler request with configured quantization/phase/return.

Distance/style automation changes only the next Generate request; it does not
regenerate every callback or morph sounding scenes. UI labels state this. While
working, show Cancel and keep playing last committed/preview snapshot. Avoid modal
progress windows. Seed entry uses a small nonmodal text popover or inline field.
Candidate conflicts link to Permissions/Activity tabs; selecting a tab does not discard
preview. Closing editor cancels background result publication and reverts preview.

## Persistence, safety and fallback

APVTS stores design knobs; scene state saves applied seed/index/version/pattern,
not transient unpublished candidates. Exact generated content is materialized, so
future planner changes do not alter old projects. Undo/Redo restores the applied
content; no new generation on restore. Unknown algorithmVersion preserves stored
materialized pattern and reports generator incompatibility only for regeneration.
Rejected/cancelled/stale requests never increment persisted candidate index or overwrite
bank. No audio file/transient classifier is required. Invalid d/r/a become documented
defaults before planning; invalid style rejected or recovered with status.

## Fixtures and acceptance

C02-T01: distance0 returns identical semantic scene including IDs/settings, all
styles/seeds. C02-T02: fixed seed/index/version reproduces exact settings/events on
GCC/MSVC; golden integer draw vectors and tolerance-documented floating results.
C02-T03: lock one field/event/operation -> all other raw draws unchanged; protected
data identical after pruning. C02-T04: assert each depth/timing/slice/topology delta
within d/r/a budget, including wrap and minimum-grid thresholds. C02-T05: 32 attempts
terminate; source capacity256 never grows257; NoChange/Conflict/Stale explicit.
C02-T06: Generate/Next/Preview doesn't change saved source; Apply one undo, Revert
and close discard, stale worker completion ignored. C02-T07: changing generation
knobs through host automation does not change current audio until explicit commit.
C02-T08: old saved seed retained; materialized v2 content loads without rerandomizing;
Release tests use explicit fail counters, not assert-only checks. Shared audio/host gates
validate audition and normal Scheduler launch integration.

## Implementation slices

Repair/review PR #9; fixed draw engine/metric fixtures; C03/C06 constrained candidate
provider; cancellable UI-worker handoff and stale tokens; snapshot/state adapter;
VariationInspector/APVTS attachments; preview/apply/undo/host tests and documentation.
