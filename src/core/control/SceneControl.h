#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Per-scene authoring state for phrase placement (C01), change permissions (C03),
// activity limits (C06) and downbeat protection (F03 subset). Plain fixed-capacity
// value types: trivially copyable, safe to hand to the audio thread by snapshot.
//
// Units: one "tick" is 1/960 of a METER beat (the beat of the meter denominator), so every
// supported subdivision (1,2,3,4,6,8,12,16 per beat) is an exact integer. This differs from
// the quarter-note ticks in docs/control-specs; see docs/alpha/ALPHA-FEATURE-STATUS.md.
namespace groovemorph::core::control
{
inline constexpr std::int32_t ticksPerBeat = 960;
inline constexpr std::size_t maxLaneEvents = 32;
inline constexpr std::size_t laneCount = 5; // equals operationCount; kept independent of it
inline constexpr std::uint8_t maxEvery = 64;

enum class WindowMode : std::uint8_t { FullPhrase = 0, Custom = 1, LastBar = 2 };

struct PlacementConfig
{
    bool enabled = false;
    WindowMode window = WindowMode::FullPhrase;
    float startPhase = 0.0f;     // [0,1) fraction of the phrase (Custom window)
    float spanPhase = 1.0f;      // (0,1] fraction of the phrase (Custom window)
    std::uint8_t every = 1;      // act on every Nth phrase, 1..64
    std::uint8_t cadenceOffset = 0;
    std::int32_t originTicks = 0; // shifts the placement timeline (phrase origin)
    std::uint8_t subdivision = 4; // editor snap grid per meter beat
    float downbeatProtectBeats = 0.0f; // 0 = off; silences all effects at each bar start
};

struct LaneEvent
{
    std::uint8_t operation = 0; // OperationType index
    bool enabled = true;
    std::int32_t onsetTicks = 0;      // [0, phraseTicks)
    std::int32_t durationTicks = ticksPerBeat; // [1, phraseTicks]
    float depth = 1.0f;               // multiplies the operation depth, [0,1]
    float sliceBeats = 0.0f;          // 0 = use the operation's slice; else [1/16,4] meter beats
    std::uint8_t priority = 50;       // 0..100, higher survives the activity limit
    bool restartCycle = false;        // true: the effect's beat grid restarts at the event onset
};

struct EventLane
{
    bool explicitEvents = false; // false: legacy uniform (operations active whole phrase)
    std::uint8_t count = 0;
    std::array<LaneEvent, maxLaneEvents> events{};
    // F06 polyrhythm: loop length of each operation's events in meter beats (0 = the phrase).
    // Events of an operation repeat every loopBeats[op] beats, independent of the phrase.
    std::array<std::uint8_t, laneCount> loopBeats{};
};

// Field lock bits per operation row (C03).
namespace lock
{
inline constexpr std::uint8_t enable = 1;
inline constexpr std::uint8_t depth = 2;
inline constexpr std::uint8_t slice = 4;
inline constexpr std::uint8_t events = 8;
inline constexpr std::uint8_t all = 15;
} // namespace lock

struct PermissionConfig
{
    std::array<std::uint8_t, laneCount> operationLocks{};
    bool placementLocked = false;
};

struct ActivityConfig
{
    bool enabled = false;
    std::uint8_t maxConcurrent = 5; // 1..5 operations scheduled at once
    std::array<std::uint8_t, laneCount> priority{50, 50, 50, 50, 50}; // 0..100, higher wins
    float minSpacingBeats = 0.0f; // 0..4: minimum distance between event starts (Reduce action)
    float maxCoverage = 1.0f;     // 0..1: maximum union of event durations as a phrase fraction
};

struct SceneControl
{
    PlacementConfig placement{};
    EventLane lane{};
    PermissionConfig permissions{};
    ActivityConfig activity{};
};

// Clamp to the documented domains. Returns true when anything changed.
bool sanitize(SceneControl& control, std::int32_t phraseTicksHint = 0) noexcept;
[[nodiscard]] bool sameControl(const SceneControl& a, const SceneControl& b) noexcept;
[[nodiscard]] bool isDefault(const SceneControl& control) noexcept;

// Event lane editing (message thread). Same-operation overlap is rejected (wraparound
// included). phraseTicks = phraseBars * beatsPerBar * 960.
enum class EditStatus : std::uint8_t { Ok, Invalid, Capacity, Overlap, Locked };
[[nodiscard]] EditStatus addEvent(EventLane& lane, const LaneEvent& event,
                                  std::int64_t phraseTicks) noexcept;
[[nodiscard]] EditStatus replaceEvent(EventLane& lane, std::size_t index, const LaneEvent& event,
                                      std::int64_t phraseTicks) noexcept;
bool removeEvent(EventLane& lane, std::size_t index) noexcept;
// True when [onset,onset+duration) of `a` intersects `b` on a circular phrase.
[[nodiscard]] bool eventsOverlap(const LaneEvent& a, const LaneEvent& b,
                                 std::int64_t phraseTicks) noexcept;

// C06 Analyze/Reduce (explicit action, never automatic). Greedy and deterministic: events of
// operations whose Events lock bit is set are mandatory and kept first; the rest are ordered by
// priority (high first, then index). An event is kept only if every kept start is at least
// minSpacingBeats away (circular) and the union of kept durations stays within maxCoverage.
enum class ReduceStatus : std::uint8_t { Valid, ConflictLocked };
struct ReduceReport
{
    ReduceStatus status = ReduceStatus::Valid;
    std::uint8_t kept = 0;
    std::uint8_t dropped = 0;
    float coverage = 0.0f; // union of kept enabled events / phrase, after reduction
    std::uint8_t peakConcurrent = 0;
};
// Returns the report; `lane` is modified only on Valid when apply is true.
ReduceReport reduceEvents(EventLane& lane, const ActivityConfig& config,
                          const PermissionConfig& permissions, std::int64_t phraseTicks,
                          bool apply) noexcept;

// Explicit command: one full-phrase event per operation in `enabledMask`. Returns events added.
std::size_t convertLegacyToEvents(EventLane& lane, std::uint32_t enabledMask,
                                  std::int64_t phraseTicks,
                                  const std::array<std::uint8_t, laneCount>* priorities = nullptr) noexcept;
} // namespace groovemorph::core::control
