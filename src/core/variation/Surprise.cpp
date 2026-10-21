#include "core/variation/Surprise.h"

#include "core/variation/CandidateBuilder.h"

#include <cmath>

namespace groovemorph::core::variation
{
bool hasAudibleEffect(const VariationSettings& settings) noexcept
{
    for (const auto& op : settings.operations)
        if (op.enabled && std::isfinite(op.depth) && op.depth >= surpriseMinAudibleDepth)
            return true;
    return false;
}

SurpriseResult buildSurprise(const VariationSettings& stored, std::uint64_t sceneSeed,
                             std::uint32_t attempt) noexcept
{
    SurpriseResult result;
    result.settings = stored;
    GeneratorSettings generator; // Generator tab defaults: Conservative, distance 1
    generator.seed = sceneSeed ^ 0x5355525052495345ULL; // "SURPRISE"
    for (std::uint32_t i = 0; i < surpriseMaxAttempts; ++i)
    {
        generator.candidate = attempt + i;
        const auto candidate = buildCandidate(stored, generator);
        if (candidate.changedOperations == 0 || !hasAudibleEffect(candidate.settings))
            continue;
        result.ok = true;
        result.settings = candidate.settings;
        result.candidate = generator.candidate;
        result.changedOperations = candidate.changedOperations;
        return result;
    }
    return result;
}
} // namespace groovemorph::core::variation
