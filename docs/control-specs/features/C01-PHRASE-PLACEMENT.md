# C01 — Phrase placement

## Objective, scope and integration

Let a user apply a scene's effects only in a chosen portion of a phrase, with an
explicit origin/subdivision and every-N-phrase cadence. Also supply the bounded
single-grid event model needed by C02/C03/C06. This closes the original missing
phrase-authoring contract; it is not F06 independent polyrhythmic clocks. Read
SHARED-CONTRACTS.md before implementing. Provider: src/core/control/PhrasePlacement.*.
Consumer: Scheduler time/grid calculations, prepared scene control segments,
RhythmEngine per-operation targets and editor phrase canvas.

Placement gates operation targets before the existing chain. It never crops live
input, adds an output gate, changes master dry/wet or copies an audio phrase. Input
and history continue while placement is inactive, so later causal reads have fresh
audio. Disabled placement preserves existing LegacyUniform output exactly.

## Required controls and exact config

Global `gridOriginTicks`: integer -1,966,080..1,966,080 ticks, default 0, step 1; display quarter notes at ticks/960. Meter-aware
text entry converts bar/beat into quarter notes. `gridSubdivision`: choices 1,2,3,4,6,8,12,16
per meter beat, default 4. Host PPQ mode uses this explicit origin; Sample Clock
uses the equivalent sample/musical accumulator anchor, never an ignored PPQ anchor.

Per scene:
- Enable placement: bool false.
- Window: FullPhrase=0, Custom=1, LastBar=2; default FullPhrase.
- Start: phase [0,1], default 0; snap to nearest subdivision for direct UI edits.
- Span: phase [1/1,966,080,1], default 1; display beats/bars and effective tick span.
- Every: integer 1..64 phrases, default 1.
- Cadence offset: integer 0..63, default 0; effective value modulo Every.
- Event lane editor: select operation, enable, onset, duration, depth [0,1],
  slice [1/16,4] beats, priority 0..100; add/delete/duplicate, snap, drag and undo.
  Advanced cycle selector: Restart at event / Continue from anchor; anchor ticks
  editable only for Continue, [0,P-1], default0. All these fields, IDs and lane
  metadata are scene state; not variable host parameters.

`PlacementConfig { bool enabled; WindowMode window; double startPhase;
double spanPhase; uint8_t every; uint8_t cadenceOffset; }`. Global grid config is
separate; no duplicated phraseBars/meter authority. Parameters are fixed in PARAMETERS.json.

## Musical rules and algorithm

Let P=phrase ticks, absolute relative position q=(quarters-origin)*960,
phraseIndex=floor(q/P), phase=positiveModulo(q,P). Evaluate continuously; do not
round q before locating a sample boundary. Cadence passes only if
positiveModulo(phraseIndex,every)==offset%every. FullPhrase uses [0,P).
Custom onset=round(startPhase*P)%P; span=max(1,round(spanPhase*P)), clamp to P.
LastBar onset=P-barTicks, span=barTicks (one-bar phrase means full phrase).
Circular windows/events wrap by splitting at P; half-open endpoints prevent double firing.

Explicit event target at a sample = operation enabled AND cadence/window AND one
enabled event of that operation containing phase. Desired amount = base operation
depth × event.depth; global intensity/dryWet stay downstream. Event.sliceBeats
replaces the base slice only for its active interval. Operation-local beat phase for
an explicit event is positiveModulo(localPhraseTicks-anchor,P)/meterBeatTicks,
where anchor=event.onsetTicks unless preserveCycleAnchor, then cycleAnchorTicks.
so a new event starts its own repeat/gate/rate cycle. Keep its ID/phase cursor across
callback splits; do not restart it at every render segment. LegacyUniform uses
original shared beat phase. Extend RhythmTiming with five operation phase values
and target/depth/slice arrays through a processTargets overload; old process API
delegates with identical shared phases and masks to preserve legacy behavior. If no event, that operation
ramps to zero. LegacyUniform retains its current internal periodic operation shape
while placement supplies the outer mask. Creating the first explicit event is an
explicit conversion command with preview and one undo entry, not silent migration. Convert LegacyUniform to one full-phrase event for each
enabled operation with depth1 and original slice, preserveCycleAnchor=true and
cycleAnchorTicks=0; preserve each operation phase
anchor. Split at selected grid/region boundaries is a separate explicit command
that retains continuity anchors when desired; phase-reset versus continuous split
is shown before commit and tested.

Same-operation overlap is rejected, including wraparound; different operations
may overlap subject to C06. Event edits validate duration 1..P, onset 0..P-1,
unique nonzero ID and count ≤256. Direct edits cannot exceed bounds or invoke an
unbounded event generator. `preparePattern(pattern,grid,placement)->PreparedPattern`
runs off-thread and returns Valid/Invalid/Capacity/Overlap. `evaluate(prepared,
position)->array<OperationTarget,5>` is noexcept, bounded and allocation-free.

On phrase length/meter changes, retain normalized placement window. Existing events
retain quarter-note onsets/durations, clamp onset to P-1/duration to P only in a
transactional revalidation; if that creates overlap, reject the new prepared event
layout and report conflict rather than silently delete events. Audio holds last
valid event snapshot; parameter/scene conflict badge offers Scale events or Undo.
Scale events is an explicit off-thread proportional transaction. Global transport
meter changes cannot be rejected at host level: render last valid event layout
projected into its previous phrase length with warning until a valid adapted
snapshot arrives; scheduling still follows current canonical transport.

## GUI and interaction

The phrase canvas shows bars, meter beats, subdivision ticks, placement shading,
playhead and five operation lanes. A draggable outer region edits Start/Span;
LastBar badge is a shortcut, not a second timing source. Clicking Placement opens
the shared inspector tab, not a modal window. Header: scene + Stored/Preview,
Enable, Window. Detail: Start, Span, Every, Offset, origin/subdivision, event
inspector. Global grid controls are explicitly labeled Global to prevent confusion.
Numeric edits work without dragging. Wrap windows have linked visible halves.
Display Requested/Effective position and history-limited slice duration.

No waveform/transient detection is implied. Keyboard arrow moves one grid unit;
Shift moves one beat; Delete removes selected unlocked event; Ctrl+D duplicates
only to a non-overlapping position or explains conflict. Window/cadence automation
updates at the next observed callback; structural edits at coherent snapshot boundaries.
Dragging makes a temporary model; release creates one transaction; Revert cancels it.

## State, lifecycle and conflicts

v1 -> placement Off + LegacyUniform; audio null migration required. New v2 saves
event IDs/pattern kind; APVTS owns scalar placement/grid values. Origin/subdivision
changes invalidate prepared boundaries and reset cadence epoch; cancel auto return.
Transport seek recomputes cadence from absolute phrase index, preserving predictability;
no history replay across actual seeks. Unknown PPQ in strict mode holds events dry.
Stopped transport is dry after ramps; Sample Clock standalone remains supported.
Permission C03 constrains generator edits; deliberate manual edits use explicit unlock
where needed. C06 prunes candidate patterns, never silently truncates manual patterns.

## Tests and acceptance

C01-T01: 4 bars, 4/4, 120 BPM/48k: LastBar enters at quarter 12 (sample 288000)
and ends at quarter 16 (384000), within one sample. Start/span wrap .875/.25
activates final half-bar plus first half-bar, not the middle. C01-T02: triplet grid,
3/8 and 7/8; signed preroll floor/modulo; exact block-end boundary deferred once.
C01-T03: Every=4/Offset=3 acts on indices 3,7,11; seek to 7 uses same decision.
C01-T04: migration null against legacy at 44.1/48/96/192k with irregular blocks.
C01-T05: 256 events succeeds, 257 rejected; overlaps/wrapped overlaps/invalid IDs
rejected transactionally. C01-T06: meter/phrase edits produce visible conflict,
explicit scale or undo restores a valid layout; no silent event loss. C01-T07:
MIDI splits, sample/PPQ selection, tempo changes, placement automation, undo/state
roundtrip and editor keyboard/narrow/wide inspector workflows. C01-T08: stress
maximum events with no callback allocations and measured callback deadlines.

## Build slices and deliverables

1. Add control types/validator and PhrasePlacementTests (portable only).
2. Extend v2 scene serializer/legacy fixtures and prepared-snapshot adapter.
3. Wire operation target masks into engine with null/impulse/gate fixtures.
4. Add stable APVTS grid/placement parameters and callback/cache tests.
5. Implement canvas/PlacementInspector and model transactions/UI fixtures.
6. Run shared build/host/RT gates; update help screenshots and traceability.
Do not remove old tests or lower thresholds. See INTEGRATION-PLAN.md for commands.
