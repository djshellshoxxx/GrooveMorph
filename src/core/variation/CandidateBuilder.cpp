#include "core/variation/CandidateBuilder.h"

#include "core/control/VariationBlend.h"
#include "core/scene/SceneBank.h"

namespace groovemorph::core::variation
{
Candidate buildCandidate(const VariationSettings& source, const GeneratorSettings& settings) noexcept
{
    Request request;
    request.source = source;
    request.seed = settings.seed;
    request.style = settings.style;
    request.candidateIndex = settings.candidate;
    request.protectedOperations = settings.protectedOperations;
    const auto planned = generate(request);

    Candidate out;
    out.settings = control::blendDistance(source, planned.settings, settings.distance);
    out.permissionRestores = control::enforcePermissions(source, out.settings);
    scene::sanitize(out.settings);
    for (std::size_t i = 0; i < operationCount; ++i)
        if (!scene::sameSettings(source.operations[i], out.settings.operations[i]))
            out.changedOperations |= 1u << i;
    return out;
}
} // namespace groovemorph::core::variation
