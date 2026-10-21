#pragma once

#include "core/OperationParams.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// A06: host-automatable "live trims" for the per-operation controls of the SOUNDING scene.
//
// The scene bank stays the only owner of stored settings. A trim is a live overlay applied to the
// effective settings of whichever scene is sounding, after morph and before placement. It never
// rewrites a stored scene, is not undoable, and is not part of scene files. With every trim at its
// default the overlay is the identity (bit-identical to a build without trims).
//
// Parameter IDs are generated from the operation index, so every operation (operationCount) gets
// them, including ones appended later:
//   op<N>Enable   choice  Scene | Off | On   (default Scene = use the scene's own Enable)
//   op<N>Depth    float   0..1               (default 1 = scene depth; multiplies the scene depth)
//   op<N>Slice    float   0.25..4            (default 1 = scene slice; multiplies the slice length)
// IDs are never renumbered or reused; a new operation takes the next index.
namespace groovemorph::core::control
{
enum class TrimKind : std::size_t { Enable = 0, Depth = 1, Slice = 2 };
inline constexpr std::size_t trimKindCount = 3;

inline constexpr float trimDepthMin = 0.0f;
inline constexpr float trimDepthMax = 1.0f;
inline constexpr float trimSliceMin = 0.25f;
inline constexpr float trimSliceMax = 4.0f;

enum class EnableTrim : std::uint8_t { Scene = 0, ForceOff = 1, ForceOn = 2 };

struct OperationTrim
{
    EnableTrim enable = EnableTrim::Scene;
    float depth = 1.0f;
    float slice = 1.0f;
};
using OperationTrims = std::array<OperationTrim, operationCount>;

// Parameter ID of one trim, e.g. operationTrimId(5, TrimKind::Depth) == "op5Depth".
[[nodiscard]] std::string operationTrimId(std::size_t operation, TrimKind kind);
// Host-visible parameter name, e.g. "Repeat / stutter: Depth trim".
[[nodiscard]] std::string operationTrimName(std::size_t operation, TrimKind kind);
// Display name of an operation; "Effect N" for operations without a registered name.
[[nodiscard]] std::string operationDisplayName(std::size_t operation);

// Values within these margins of the default count as the default, so host round-trips of the
// normalised value (which can be off by an ulp) never change the sound.
[[nodiscard]] bool isIdentity(const OperationTrim& trim) noexcept;
[[nodiscard]] bool isIdentity(const OperationTrims& trims) noexcept;

// Applies the trims to a copy of the sounding scene's settings. Non-finite trim values are treated as
// the default. Enable: Off clears, On sets. Depth: multiplied, clamped to 0..1. Slice: multiplied,
// clamped to the scene slice range (1/16..4 beats). Only the operation rows are touched.
void applyTrims(VariationSettings& settings, const OperationTrims& trims) noexcept;
} // namespace groovemorph::core::control
