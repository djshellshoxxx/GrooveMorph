#include "core/variation/VariationPlanner.h"
#include <algorithm>
#include <cmath>
namespace groovemorph::core::variation {
namespace {
std::uint64_t next(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    auto z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
float unit(std::uint64_t& state) noexcept {
    return static_cast<float>((next(state) >> 40) * (1.0 / 16777216.0));
}
float bounded(float f, float fallback, float minV, float maxV) noexcept {
    return std::isfinite(f) ? std::clamp(f, minV, maxV) : fallback;
}
}
Result generate(const Request& request) noexcept {
    Result result{};
    result.settings = request.source;
    auto rng = request.seed ^ (static_cast<std::uint64_t>(request.candidateIndex) * 0xd6e8feb86659fd93ULL);
    const auto style = request.style;
    const float probability = style == Style::Conservative ? 0.25f : style == Style::Creative ? 0.55f : 0.85f;
    const float depthSpread = style == Style::Conservative ? 0.12f : style == Style::Creative ? 0.30f : 0.55f;
    for (std::size_t i = 0; i < operationCount; ++i) {
        // Always advance PRNG even on protected operations so subsequent
        // mutations are independent of the protection mask.
        const float choose = unit(rng);
        const float depthOffset = 2.0f * unit(rng) - 1.0f;
        const float sliceChoice = unit(rng);
        const float toggle = unit(rng);
        if ((request.protectedOperations & (1u << i)) || choose >= probability)
            continue;
        auto& op = result.settings.operations[i];
        op.depth = std::clamp(bounded(op.depth, 1.0f, 0.0f, 1.0f)
                                  + depthSpread * depthOffset, 0.0f, 1.0f);
        // Power-of-two subdivisions are predictable against host beat grids.
        constexpr float slices[] = {0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
        if (sliceChoice < (style == Style::Conservative ? 0.15f : 0.5f))
            op.sliceBeats = slices[next(rng) % 7u];
        else
            op.sliceBeats = bounded(op.sliceBeats, 0.25f, 0.0625f, 4.0f);
        if (toggle < (style == Style::Experimental ? 0.22f : 0.05f))
            op.enabled = !op.enabled;
        result.mutatedOperations |= 1u << i;
    }
    return result;
}
}
