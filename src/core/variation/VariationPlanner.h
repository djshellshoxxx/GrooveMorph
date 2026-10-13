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
    // Bit i prevents any changes to operation i.
    std::uint32_t protectedOperations = 0;
};
struct Result {
    VariationSettings settings{};
    std::uint32_t mutatedOperations = 0;
};
// Portable, bounded and deterministic. No allocation, host dependency or global RNG.
// This MVP-scene adapter operates on operation settings; event-level mutation is
// deferred until canonical PhraseEvent timing representation is integrated.
[[nodiscard]] Result generate(const Request& request) noexcept;
}
