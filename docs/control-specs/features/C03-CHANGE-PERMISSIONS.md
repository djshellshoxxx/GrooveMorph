# C03 — Change permissions

## Objective and whole-plugin role

Let users preserve useful parts of a scene while generating alternatives. Permissions
constrain data mutation, not live audio amplitude. They sit between source capture
and C02 planning and are checked again after C06 pruning, before Preview/Apply.
They do not add a DSP stage, a bass detector or the separate F03 groove-protection
engine. Source: src/core/control/ChangePermissions.*; consumers C02/C06, scene
validation/undo and PermissionsInspector. Requires C01 stable event IDs/ticks.

## Complete control model

Per scene, retain five operation rows × four lock toggles: Enable, Depth, Slice,
Events. Row Lock all/Unlock all are transactions, not persisted redundant fields.
Global locks: phrase placement, event creation/deletion and global operation
settings. Each event has a 4-bit lock mask: Timing, Depth, Slice, Enable/Delete.
At most eight protected temporal regions, each onset [0,P-1], duration [1,P],
operationMask [1,31] and same four mutation bits. Regions wrap like C01 windows.
All locks default false; no automatic protection inferred from names/drum content.
Protect first beat creates an ordinary region [0,meterBeatTicks), all operations,
all fields; width control 1/16..4 beats, default 1, with displayed effective length.
Clear protection, copy permissions to scene and reset unlocked are explicit actions.

Exact types: `FieldLock : uint8_t { Timing=1, Depth=2, Slice=4, EnableDelete=8 }`;
`PermissionRegion { uint32_t id; int64_t onsetTicks; int64_t durationTicks;
uint8_t operationMask; uint8_t fields; }`; `PermissionConfig { array<uint8_t,5>
operationFields; bool placementLocked; bool topologyLocked; bool globalsLocked;
array<uint8_t,256> eventFields; array<PermissionRegion,8> regions; uint8_t regionCount; }`.
Event mask slots are aligned with current pattern indices in prepared snapshot;
stored state maps masks by event ID. Invalid missing IDs are rejected/reported.
No permission toggle is host automation; all are named scene-state controls.
A region locks an intersecting event as a whole. If a full-phrase event intersects
the first-beat region, the entire event is protected; show that impact and offer
C01 Split at region boundaries before locking to permit changes outside it.

## Permission semantics and conflict rules

A requested edit is legal only if every applicable operation/event/region/global
permission allows it. Union lock bits; stricter scope wins. Protected region
intersections apply to old AND proposed event intervals, so events cannot be
moved out of protection to evade a lock or moved into it while changing locked fields.
Same-operation global settings changes that would alter a protected event are
blocked for that operation; C02 can still change unprotected event-local fields.
This preserves stored event/control data, not a promised bit-identical protected
audio waveform after unrelated upstream effects or a scene phase change.

For operation-row Events, the Timing bit additionally forbids event creation/deletion for that lane; per-event Timing does not lock topology, while EnableDelete does.

Locked EnableDelete forbids toggling, removing, replacing or indirectly pruning
that event. Lock Timing fixes onset/duration/cycle anchor and cadence/placement influences that
would relocate its activity; when such influences are global, block the corresponding
candidate global mutation. topologyLocked forbids all event add/delete, even unlocked.
Protect First Beat acts on event metadata; LegacyUniform requires conversion to
explicit events before time-region event locks become available. Operation locks
still work on legacy settings. Show disabled region tools with this explanation.

Provider API: `checkEdit(source,candidate,permissions,grid)->PermissionReport`,
`canMutate(eventId,field,...)->bool`, and `lockedActivity(...)->summary`. Report has
fixed eight conflict records + total count; each record operation/event/field/reason.
Off-thread full validation ≤256 events ×8 regions; callback receives already valid
patterns and no permission solver. Masking one field never advances another field's
PRNG draws; enforce independently in C02. There is no automatic unlocked fallback.

If locks make C06 constraints impossible, return Conflict and preserve source;
Preview/Apply disabled until user adjusts limits/unlocks. A direct manual edit to a
locked item requires an explicit Unlock action or a one-edit override confirmation
that clearly records whether the lock remains. Default: unlock first; no silent
manual bypass. Host automation of performance knobs is intentionally outside
mutation permissions; label “Protect from generation/editing”, not “freeze sound”.

## GUI integration

Small padlock icons on each operation row/event open Permissions tab with scope
already selected. Right slide-out/wide or bottom tray/narrow follows shared GUI.
Matrix columns: Enable, Depth, Slice, Events. Selected-event subsection: Timing,
Depth, Slice, Enable/Delete. Region subpanel: Protect first beat, width, operation
selection, lock-field checkboxes; Add/Edit/Delete regions with miniature phrase strip.
Use closed lock + text explanation, not color alone. Hover explains exact scope.
Summary: “3 operations / 2 events / first beat protected”. Never require a popup
for basic locking. Conflict explanation may use a nonmodal popover listing IDs,
fields and actions Open Activity / Unlock selected / Cancel.

Generate panel shows inherited lock summary and link to this tab. Changing locks
marks any candidate stale; it cannot apply until regenerated/revalidated. Permission
editing during audition works on the audition scene only and reverts with it.
Commit candidate preserves the selected scene's permissions; generation does not
rewrite its own rules. Copy permissions does not copy audio/operation content.

## Persistence and lifetime

v2 saves masks and regions by scene/event ID; v1 loads unlocked, regionCount 0.
A structural edit maps masks to retained IDs, removes a mask only when an unlocked
event is explicitly deleted, and gives a new ID unlocked defaults. Scene duplicate
copies permissions with an ID remap transaction. APVTS controls do not duplicate
permission masks. Undo restores complete permission/content state; automation does
not create permission edits. No locks on callback; prepared snapshots carry status
counts only. Transport/reset/bypass do not alter persistent permissions.

## Tests and exact acceptance

C03-T01: all 16 field masks and five operations enforce each field independently;
locked data remains byte-equivalent after generation (excluding nonsemantic ordering).
C03-T02: event moved across protected region edge still fails for locked fields;
wrap regions and touching endpoints have C01 half-open behavior. C03-T03: first-beat
width 1 in 7/8 = 480 ticks, not 960. C03-T04: region capacity8 succeeds,9 rejected;
invalid op mask/unknown ID rejected. C03-T05: protected operation0 leaves random
results for other unlocked operations unchanged. C03-T06: locked source already
exceeds cap -> conflict, no mutation and disabled Apply. C03-T07: locked manual edit
cannot slip through UI, serializer, copy/duplicate or prune paths; legacy conversion
required for region tools. C03-T08: preview/revert/undo/state roundtrip and focus/
keyboard matrix work at both widths; no DSP-output change when only locks change.

## Implementation slices

1. Provider types/edit validator + ChangePermissionsTests.
2. v2 ID-mask migration and transaction integration.
3. C02 mutation query + C06 final validation.
4. padlocks/PermissionsInspector/candidate-stale UI status.
5. end-to-end locked edit/preview/undo and shared release checks.
