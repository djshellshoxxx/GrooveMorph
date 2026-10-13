#pragma once
#include "core/OperationParams.h"
#include <cstdint>
namespace groovemorph::core::variation {
enum class Style : std::uint8_t { Conservative, Creative, Experimental };
struct Request {
    VariationSettings source{};
    std::uint64_t seed = 0;
    Style style = Style::Conservative;
    std::uint32_t candidateIndex = 0;
    // Bit i preserves operation i exactly.
    std::uint32_t protectedOperations = 0;
};
struct Result {
    VariationSettings settings{};
    std::uint32_t mutatedOperations = 0;
};
// Stateless, allocation-free deterministic variation of the existing operation
// settings. Intended for future UI integration; does not mutate a SceneBank.
// Protected and randomly unselected operations preserve their fields exactly,
// including invalid source floats. Selected operations alone normalize depth
// to finite 0..1 and sliceBeats to finite 1/16..4 (nonfinite fallbacks: 0.5, 0.25).
// Callers must validate the complete scene before publishing it to audio.
[[nodiscard]] Result generate(const Request& request) noexcept;
}
