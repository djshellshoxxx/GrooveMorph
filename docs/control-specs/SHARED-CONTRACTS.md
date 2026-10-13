# Shared contracts, state, timing, GUI and DSP

## Baseline and invariants

C++20, CMake, pinned JUCE 8.0.7, VST3 and JUCE Standalone, Windows/Linux.
Preserve existing parameter IDs and enum order: Repeat, ReverseSlice, Gate,
HalfTime, TapeStop. Existing user settings and legacy audio behavior must survive.
Retain one canonical Scheduler owned by the callback, one RhythmEngine and scene
model owned off the callback. Modules under src/core/control/ use standard library
and fixed-capacity data; editor/adapters remain JUCE-specific.

Signal path remains input -> Repeat -> Reverse -> Gate -> HalfTime -> TapeStop
-> master intensity × dry/wet -> existing bypass ramp -> output. Each history
stage continues to store its stage input. C01/C06 decide which operations are
active; C02/C03 produce/edit control data off-thread; C04 changes musical read phase;
C05 schedules scene/dry-overlay changes. None adds an unrelated audio-effect stage.
No new full capture, lookahead, output limiter, time stretch or band splitting.

## Exact common domains and proposed C++ boundary

All musical ticks represent quarter notes, not denominator beats. kPpqTicks=960.
One meter beat = 960*4/denominator ticks. Allowed subdivisions per meter beat:
1,2,3,4,6,8,12,16; all divide the supported meter-beat tick lengths exactly.
Default 4. Phrase length uses existing phraseBars and resolved meter. Maximum
32*4*16=2048 quarters, 1,966,080 ticks. Store tick positions in signed int64_t.
Negative preroll uses mathematical floor division and positive modulo. UI bar/beat
values are one-based; stored ticks and event indices are zero-based. Validate
finite tempo/rate and 64-bit arithmetic before calculation.

The implementation must introduce these public provider types, in
src/core/control/ControlTypes.h, not shadow similarly named existing scene types:

```cpp
namespace groovemorph::core::control {
inline constexpr std::int64_t ppqTicks = 960;
inline constexpr std::size_t maxEvents = 256;
enum class PatternKind : std::uint8_t { LegacyUniform, ExplicitEvents };
struct PhraseEvent {
    std::uint32_t id; // unique nonzero within scene; stable after edits
    OperationType operation;
    std::int64_t onsetTicks;
    std::int64_t durationTicks;
    float depth; // finite [0,1]
    float sliceBeats; // finite [1/16,4] meter beats
    std::uint8_t priority; // 0..100, default 50
    bool enabled;
    bool preserveCycleAnchor;
    std::int64_t cycleAnchorTicks; // [0,phraseTicks); used when preserving
};
struct EventPattern {
    std::array<PhraseEvent, maxEvents> events;
    std::uint16_t count;
    PatternKind kind;
};
enum class RequestOrigin : std::uint8_t { AutoReturn, Midi, Host, User };
enum class PhasePolicy : std::uint8_t { Preserve, Restart, AlignBeat };
enum class ReturnTarget : std::uint8_t { Previous, A, B, C, D, Dry };
struct SceneLaunchRequest {
    std::uint8_t scene; // 0..3
    TransitionMode quantization;
    PhasePolicy phase;
    RequestOrigin origin;
    std::uint64_t sequence;
};
struct MusicalPosition {
    double quarterNotes; // finite; full precision, not integer-tick rounded each callback
    std::uint64_t epoch;
    bool advancing;
    bool valid;
};
}
```

Config/provider-specific exact members are in each feature. All configs are value
objects, validated before publishing. Functions return explicit status enums,
never exceptions in the callback. EventPattern arrays are bounded, not vectors.
Pattern edits use IDs; reorder/sort never changes IDs. IDs wrap only by finding an
unused nonzero value off-thread; fail Capacity when all required slots are occupied.

## Musical position, transport and scheduling

For Automatic/Host PPQ use usable host PPQ. For sample fallback integrate a musical
quarter-note accumulator from resolved BPM and processed sample count; at a seek
or mode/meter change re-anchor from canonical resolved grid, increment epoch, and
cancel transient returns. Do not rebuild elapsed musical duration from absolute
samples × new tempo after a tempo change. For strict Host PPQ missing PPQ, hold
quantized work and ramp rhythmic processing dry. An explicitly stopped host
suspends musical elapsed-time accumulation; Sample Clock without host play state
advances as explicitly selected standalone preview.

C04 supplies event-local phrase phase for C01 windows/events; C01 cadence remains
on the absolute canonical phrase index. Explicit event operation phase starts at
its onset, with continuity anchors only for explicit split-preserve actions.

Quarter-note position is continuous double; ticks describe event design only.
Boundary sample offsets use nearest-sample rounding and the existing half-sample
PPQ edge rule. At callback/sub-block boundaries maintain a next-boundary cursor
and half-open intervals: [blockStart,blockEnd). A rounded event at blockEnd belongs
to the next segment, including exactly-on-boundary offset zero. Avoid repeated
round-to-tick position estimates that move events by many samples.

Scheduler must extend existing request/process APIs with context/source/phase and
one return token; do not replace it wholesale. MIDI request offsets remain honored.
UI requests become timestamped at the next callback start. Global/discrete APVTS
changes are block-observed in the pinned adapter; do not advertise intra-block
sample-accurate automation from raw atomics. Musical events still split at their
computed sample offsets. A future automation-point bridge is separate, tested work.

Same-frame ordering: validated restore/epoch reset first; non-return scene requests
(User > Host > Midi, highest sequence wins ties); then auto return only if no
external launch at that frame; then event ends before event starts; then masks;
then audio. Freeze a pending launch's quantization/phase/config on request. Global
clock-mode changes recompute its boundary; settings edits don't silently rewrite it.
Latest queued launch wins as today. Cancellation does not imply universal force.

## Threading and bounds

One model publisher holds scene/undo mutex off-thread. Audio obtains immutable
trivially-copyable prepared snapshots through the current nonblocking handoff or a
verified bounded mailbox; no strings, locks waiting, copies of audio rings, file I/O,
allocation/deallocation, APVTS tree access, host notification or GUI calls in process.
Cache raw scalar pointers once. Multiple UI/host producers must serialize through
one publisher; do not treat an SPSC FIFO as MPSC. Fixed command capacity 64;
drop newest noncritical edit with overflow status, retain latest scene selection
in a separate atomic mailbox; cancellation/reset has a reserved critical flag.

Maximum 256 stored events/scene, five live operation lanes. Explicit same-operation
events cannot overlap on the circular phrase; touching endpoints allowed. One
control segment list holds at most 512 distinct boundary frames per prepared audio
segment (≤256 starts + 256 ends); same-frame boundaries coalesce. Host buffers
larger than prepared maxBlock are processed in preallocated chunks. Active lane
lookup is O(5) per sample; event boundary scanning is O(256) per audio chunk maximum.
No periodic generation inside the audio callback. No silent event truncation.
Reject a model that exceeds bounds; report status without disturbing current audio.

Maintain current history storage and causal clamping. Phase changes do not clear
valid history; actual host seek/reset does. Adjusting read phase uses existing
bounded voice/ramp machinery, not another chain or a full-history copy. At most two
retiring read taps per operation, with documented oldest-retiring-tap eviction on
repeated transitions. This is operation-specific smoothing, not a claim of a
phase-coherent whole-chain crossfade. Report current zero algorithmic latency.

## Persistence and automation authority

Append stable parameter IDs from PARAMETERS.json; never rename the ten current IDs.
For per-scene scalar performance controls use literal scene0..scene3 prefixes.
Selected edit scene only selects widgets; parameter identities do not change when
selection changes. APVTS is the sole persistent authority for those scalar values.
Event patterns, IDs, permissions, named snapshots and exact 64-bit seed/candidate
index are scene state. Do not maintain independently authoritative duplicate scalar
copies in serialized SceneBank; save a coherent adapter snapshot that links them.
Generation distance controls are global design controls sampled at Generate, not
live wet processing. permissions are scene state, not automatable GUI switches.

Scene format v2 appends explicit pattern/permission data; v1 loads LegacyUniform,
permissions unlocked, placement disabled, Preserve phase, return Off and activity
limits disabled. This must null against the legacy runtime on fixed fixtures.
Missing new APVTS parameters get their documented defaults even in an already-used
instance. Unknown fields ignored with recovery information; invalid event count,
ID, numeric input or overlaps are rejected transactionally with defaults/report.
Keep 1 MiB host envelope cap; 4×256 events must fit it in worst-case text fixtures.
Parameter/state restore validates outside process and publishes one coherent config.
Transient queues, audition audio history, pending return and phase cursors are not
saved. Preserve recovery reporting. Older builds do not support v2, so export a
clear version notice; don't promise bidirectional preset compatibility.

Direct structural edits add one scene transaction per drag/command. Candidate Apply
is one undo transaction. A unified editor transaction stores event/permission changes
and before/after APVTS scalars for affected scene; UI undo applies scalar host gestures
off-thread and publishes the combined model. Host automation never creates scene-undo
entries. Global existing controls keep their current host/undo semantics. Undo must
not arm or resurrect a return. Solve existing A03 host requested/actual scene feedback
before claiming integration complete; keep actual and requested statuses distinct.

## Common GUI contract

Retain current header and A–D performance strip. Add one contextual inspector with
tabs Placement, Variation, Permissions, Transition, Return, Activity. Only one open
tab at a time. At width ≥1120 dock a 320 px right slide-out; at smaller widths use a
bottom expandable tray with independently scrolling contents. Do not overlay global
controls or the performance strip. Keep minimum 760×690; collapse main detail rows
into a viewport when tray opens. Save selected tab/open state as editor preferences,
not audio scene parameters. No modal popup for live controls. A small nonmodal popover
is allowed for reset confirmation, seed entry or conflict explanation, never required
for performing a fill. Escape closes inspector; restore focus to opening button.

All sliders have editable numeric text, units, reset-to-default, tooltips, keyboard
increments and accessible names. Show scene name + Stored/Preview/Unsaved scope.
Color supplements labels/icons. Preview audition should show its actual audible scene
separately from committed active scene. A single Generate/Preview/Apply/Revert group
controls candidate commits; closing editor reverts uncommitted preview as today.
Read-only status is refreshed at 10 Hz, not from the callback. Drawer animation stops
when hidden, and respects reduced-motion preferences. Help links open local/manual
content through the UI thread. Do not place implementation details in product labels.
