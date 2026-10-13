# C06 — Effect activity limits (selected sixth control)

## Objective and chain placement

Limit how many effects can be scheduled together, how close event starts can be,
and how much of a phrase is affected. This is compositional restraint, independent
of master intensity and generation distance. Limits govern C02 candidates and
validate explicit user patterns from C01. They mask operation targets before
Repeat/Reverse/Gate/HalfTime/TapeStop, not a compressor/limiter after the chain.
Provider: src/core/control/ActivityLimiter.*; consumers candidate planner,
prepared pattern compiler, ActivityInspector and callback operation target adapter.
Requires C01 events and C03 permission queries.

## Controls, ranges and exact config

Per-scene APVTS controls:
- Enable: bool false, legacy output unchanged.
- Maximum scheduled operations at once: integer 1..5, default 5.
- Minimum event-start spacing: 0..4 meter beats, default 0, increment 1/16 beat.
- Maximum phrase coverage: 0..100%, default 100; normalized [0,1].
- Enforcement scope: GeneratedOnly=0, AllExplicitPatterns=1; default GeneratedOnly.

Each event's priority 0..100/default50 is scene state in the C01 event inspector.
Actions: Analyze, Preview reduced pattern, Apply reduction; never destructively
prune a stored scene merely because a limit changed.
`ActivityConfig { bool enabled; uint8_t maxConcurrent; float minSpacingBeats;
float maxCoverage; Scope scope; }`; `ActivityReport { Status status;
uint16_t kept,dropped; uint8_t peakScheduled; float coverage;
array<uint32_t,256> droppedIds; uint16_t droppedCount; }`.
Report statuses Valid, ConflictLocked, RequiresExplicitEvents, EmptyByPolicy, Invalid.

## Definition and deterministic enforcement

Coverage is the circular union duration of enabled scheduled event intervals divided
by phrase duration, not RMS energy or how often Gate closes. Concurrent count is
number of distinct operations with active scheduled targets, not number of objects
or CPU threads. Per-operation nonoverlap is already enforced by C01. Minimum spacing
compares event starts across all lanes, including last-to-first gap across phrase
wrap. Starts at the same tick violate any positive spacing, even on different lanes.

Analyze/prune off-thread in integer ticks after placement projection. Include
placement window intersection; cadence decides which phrase instances are active,
but compute limits per active phrase so Every=8 cannot hide an overfull active phrase.
Sort mandatory locked events first; then remaining by priority descending, stable
ID ascending. Greedily include an unlocked event only when its inclusion preserves
all three limits; no trimming, shifting or rewriting depth/slice by this feature.
Locked events cannot be removed. If locked subset violates any limit, ConflictLocked,
no new candidate, source unchanged. Identical source/limits/permissions produce
identical keep/drop decisions. If zero coverage requested, result empty only when
no locked audible event exists; expose EmptyByPolicy, not a failed engine.

Bounded intervals: at most 512 split intervals (256 wrapping events); compute union
and overlap via off-thread sorted endpoints, ends before starts. Use strict integer
comparisons with no float thresholds except coverage*P converted by floor, making
it a true upper bound. min spacing converts beats via meter beat ticks and ceil.
At runtime consume compiled masks from fixed storage. The cap limits scheduled
targets; retiring smoothing taps can overlap briefly and are not a CPU/concurrency
guarantee. UI label must say “Scheduled effects at once”. Existing hard live lane
bound remains five. Do not advertise a performance governor from this control.

LegacyUniform: MaxConcurrent can deterministically select enabled operations by
fixed enum order with operation-lock precedence. Coverage/spacing require explicit
events; show RequiresExplicitEvents and offer C01 conversion preview. No false
coverage estimate based on undocumented gate duty/read behavior.

GeneratedOnly applies to candidate creation, never muting current stored audio.
AllExplicitPatterns adds a non-destructive compiled playback mask. A cap change is
observed at a callback, marks a planning revision dirty through atomic flags, and
is compiled off-thread; apply newest valid prepared mask at next phrase boundary.
Show Pending limits until then. Keep last valid mask on conflict or compilation
lag; never compute a pruning solver inside process or corrupt the stored pattern.
Automation of limits follows this boundary/pending contract; it is not immediate
sample-level automation. New source/permissions changes invalidate stale compilation
results by revision token. Preview reduced pattern/Apply may materialize deletions
as one undo transaction only when C03 permits.

## GUI

Activity tab in common inspector; quick collapsed badge “2 effects / 50% phrase”.
Controls: Enable, Scheduled effects, Start spacing, Phrase coverage, Scope.
Read-only Requested vs Actual counts/coverage; phrase mini-map displays retained
and dropped events with explicit labels. Changes in GeneratedOnly update next
candidate; changes in AllExplicitPatterns show next-phrase Pending state.
Conflict banner explains locked IDs and offers Open Permissions / Adjust limits;
no auto-unlock. Legacy conversion button opens Placement, preserving preview state.
This feature has its own tab, not a standalone popup. Accessible labels distinguish
activity coverage from wet/dry; no audio peak meter is implied.

## State and lifecycle

v1 -> Enable false, 5,0,1,GeneratedOnly. APVTS scalar authority; stored pattern
remains unchanged until explicit Apply. Compiled masks are transient and rebuild
after restore. Undo restores explicit reductions/associated scalar changes through
shared transaction rules; playback automation does not enter undo. Host stop holds
last mask but operations ramp dry. Epoch changes cancel pending return; newest
valid mask recompiles for current meter/phrase, never carries stale absolute ticks.
Preview limits belong to candidate source revision; don't write global host bypass.

## Tests / measurable acceptance

C06-T01: Repeat [0,4), Reverse [2,6), Gate [3,5) quarters; cap2 keeps higher-priority
Repeat/Reverse and rejects Gate when overlap3 would occur. C06-T02: same-frame ends
before starts; wraparound overlap/spacing measured across phrase edge. C06-T03:
coverage cap.5 in 4-quarter phrase cannot keep disjoint union >1920ticks; touching
intervals union correctly. C06-T04: locked first three overlapping operations and
cap2 -> ConflictLocked; no silent deletions/unlock. C06-T05: cap0 coverage -> valid
empty on unlocked input, conflict on locked activity. C06-T06: stable ordering independent
of source event-array order; random generator does not get additional RNG draws
from pruning. C06-T07: stale compiled revision ignored, pending masks apply exactly
once on next phrase; negative/malformed/NaN configs recover. C06-T08: generated-only
limit edit leaves stored audio/state untouched; explicit Apply one undo; legacy
spacing/coverage tools disabled with reason. C06-T09: UI scope labels/pending/conflict,
state v1 null migration and 256-event callback/RT tests.

## Implementation order

Validator/union/greedy provider + ActivityLimiterTests; permission integration;
compiled mask revision handoff; stable parameters; ActivityInspector/candidate
feedback; processor and maximum-load QA. Commands/gates: INTEGRATION-PLAN.md.
