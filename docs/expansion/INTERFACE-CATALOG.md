# Shared integration interfaces — initial proposals

All names are proposed, not claims about existing source. Do not replace existing core contracts without explicit compatibility migration.

```cpp
struct MusicalTime { int64_t sample; double ppq; double bpm; int numerator; int denominator; uint64_t epoch; bool ppqValid; bool running; };
struct EventId { uint64_t high, low; };
enum class Operation : uint8_t { Gate, Repeat, Stutter, Reverse, Rate };
struct PhraseEvent { EventId id; int32_t lane; int64_t onsetTicks; int64_t durationTicks; Operation op; float amount; uint32_t flags; };
struct SceneId { uint64_t high, low; };
struct SceneSnapshot { SceneId id; uint32_t schema; uint64_t revision; /* owned immutable bounded event array */ };
struct Command { uint64_t sequence; int32_t sampleOffset; uint32_t triggerSource; uint32_t kind; SceneId target; };
struct PlanResult { uint32_t status; uint32_t eventCount; uint32_t generation; };
// Proposal: exact snapshot ownership, numeric field range and lifetime must be frozen after audit.
```

## Mandatory invariants
Every sample offset is within [0, currentBlockSamples]; every duration positive and bounded; event IDs unique in a scene; operations are versioned; all probabilities finite and clamped; musical tick rate is stable and migration-safe; overflow is checked before rational multiplication; event sorting is stable by timestamp, priority then ID. Parameters are normalized by explicit mapping and cannot be silently renumbered.

## Ownership
Planner receives read-only retained snapshot on worker thread and returns validated immutable plan. Audio thread consumes a fixed-capacity plan without allocation and publishes only counters/state. Lifetime reclamation is performed by non-audio owner after audio acknowledgment. The only allowed shared mutator is a serialized command handler; no cross-thread raw pointers to mutable vectors.

## Contract freeze checklist
Cross-reference existing src/core types and JUCE Processor bindings, settle naming and UUID layout, confirm maximum events/lanes, establish forward-compatible binary schema, add compile-only contract fixtures and test producer/consumer publication.