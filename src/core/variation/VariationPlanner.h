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
[[nodiscard]] Result generate(const Request& request) noexcept;
}
