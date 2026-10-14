#pragma once

#include "core/OperationParams.h"
#include "core/Scheduler.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Shared provider types for the six musical controls (docs/control-specs/SHARED-CONTRACTS.md).
// Standard library and fixed-capacity data only; no JUCE, no allocation.
namespace groovemorph::core::control
{
// All musical ticks are quarter-note based; one meter beat = ppqTicks * 4 / denominator.
inline constexpr std::int64_t ppqTicks = 960;
inline constexpr std::size_t maxEvents = 256;
inline constexpr std::uint8_t maxPriority = 100;

enum class PatternKind : std::uint8_t
{
    LegacyUniform,
    ExplicitEvents
};

struct PhraseEvent
{
    std::uint32_t id = 0; // unique nonzero within a scene; stable after edits
    OperationType operation = OperationType::Repeat;
    std::int64_t onsetTicks = 0;
    std::int64_t durationTicks = 0;
    float depth = 1.0f;       // finite [0,1]
    float sliceBeats = 0.25f; // finite [1/16,4] meter beats
    std::uint8_t priority = 50; // 0..100
    bool enabled = true;
    bool preserveCycleAnchor = false;
    std::int64_t cycleAnchorTicks = 0; // [0,phraseTicks); used when preserving
};

struct EventPattern
{
    std::array<PhraseEvent, maxEvents> events{};
    std::uint16_t count = 0;
    PatternKind kind = PatternKind::LegacyUniform;
};

enum class RequestOrigin : std::uint8_t
{
    AutoReturn,
    Midi,
    Host,
    User
};

enum class PhasePolicy : std::uint8_t
{
    Preserve,
    Restart,
    AlignBeat
};

enum class ReturnTarget : std::uint8_t
{
    Previous,
    A,
    B,
    C,
    D,
    Dry
};

struct SceneLaunchRequest
{
    std::uint8_t scene = 0; // 0..3
    TransitionMode quantization = TransitionMode::Immediate;
    PhasePolicy phase = PhasePolicy::Preserve;
    RequestOrigin origin = RequestOrigin::User;
    std::uint64_t sequence = 0;
};

struct MusicalPosition
{
    double quarterNotes = 0.0; // finite; full precision, not integer-tick rounded each callback
    std::uint64_t epoch = 0;
    bool advancing = false;
    bool valid = false;
};
} // namespace groovemorph::core::control
