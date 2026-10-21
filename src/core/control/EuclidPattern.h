#pragma once

#include "core/OperationParams.h"
#include "core/control/SceneControl.h"

#include <array>
#include <cstddef>
#include <cstdint>

// N03 Euclidean pattern generator (docs/specs/N03-EUCLID-CHANCE.md). Portable, fixed capacity, no
// allocation. Message-thread authoring tool: it only produces lane events; playback is unchanged.
namespace groovemorph::core::control
{
inline constexpr int euclidMaxSteps = 32;
using EuclidSteps = std::array<bool, euclidMaxSteps>;

struct EuclidParams
{
    std::uint8_t steps = 8;          // 1..32
    std::uint8_t pulses = 3;         // 0..steps
    std::int8_t rotation = 0;        // modulo steps; positive moves hits later
    std::uint8_t operation = 0;      // target operation lane
    std::uint8_t subdivision = 4;    // steps per meter beat: 1,2,3,4,6,8,12,16
    std::uint8_t gatePercent = 50;   // 10..100 of one step
    bool repeat = true;              // tile across the lane ring
    std::uint8_t accentEvery = 0;    // 0 = off; every Nth pulse of a tile is an accent
    float accentDepth = 1.0f;        // 0..1
    float softDepth = 0.6f;          // 0..1, non-accent pulses when accents are on
    std::uint8_t density = 100;      // 0..100 chance of non-accent events (all events when accents are off)
};

// Clamps every field to its documented domain. Returns true when anything changed.
bool sanitize(EuclidParams& params) noexcept;

// Canonical Bjorklund pattern (rotation 0), hit at step 0 whenever pulses > 0. Inputs are clamped.
[[nodiscard]] EuclidSteps bjorklund(int steps, int pulses) noexcept;
// Bjorklund pattern with rotation applied: out[i] = base[(i - rotation) mod steps].
[[nodiscard]] EuclidSteps euclidSteps(const EuclidParams& params) noexcept;
// Ticks of one grid step (960 / subdivision; an unsupported subdivision falls back to 4).
[[nodiscard]] std::int32_t euclidStepTicks(std::uint8_t subdivision) noexcept;

enum class EuclidStatus : std::uint8_t { Ok, OperationLocked, Invalid };

struct EuclidReport
{
    EuclidStatus status = EuclidStatus::Ok;
    std::uint16_t added = 0;
    std::uint16_t removed = 0;          // unlocked events of the target operation that were replaced
    std::uint16_t keptLocked = 0;       // locked events of the target operation that stayed
    std::uint16_t skippedRegion = 0;    // pulses inside a protected region
    std::uint16_t skippedOverlap = 0;   // pulses overlapping another event (incl. locked ones)
    std::uint16_t skippedCapacity = 0;  // pulses dropped because the lane was full
    bool convertedLegacy = false;       // other enabled operations were turned into full-phrase events
};

// Replaces the unlocked events of params.operation in `scene` by the Euclidean pattern.
// phraseTicks = beatsPerBar * phraseBars * 960. On OperationLocked/Invalid the scene is untouched.
// sceneIndex only seeds the chance dice (ensureChanceSeed) when generated events use chance < 100.
EuclidReport applyEuclid(VariationSettings& scene, const EuclidParams& params, std::int64_t phraseTicks,
                         std::size_t sceneIndex = 0) noexcept;

// Candidate = copy of `source` with the pattern applied, then enforcePermissions(source, candidate, true,
// phraseTicks): the same lock path as manual edits. `source` is never modified.
[[nodiscard]] VariationSettings buildEuclidCandidate(const VariationSettings& source, const EuclidParams& params,
                                                     std::int64_t phraseTicks, EuclidReport* report = nullptr,
                                                     std::size_t sceneIndex = 0) noexcept;

// Removes the unlocked events of `operation` (same lock rules).
// OperationLocked/Invalid leave the scene untouched.
EuclidReport clearEuclidOperation(VariationSettings& scene, std::uint8_t operation) noexcept;
[[nodiscard]] VariationSettings buildClearCandidate(const VariationSettings& source, std::uint8_t operation,
                                                    std::int64_t phraseTicks, EuclidReport* report = nullptr) noexcept;
} // namespace groovemorph::core::control
