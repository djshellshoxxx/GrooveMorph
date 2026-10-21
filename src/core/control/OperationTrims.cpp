#include "core/control/OperationTrims.h"

#include "core/scene/SceneBank.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace groovemorph::core::control
{
namespace
{
constexpr float identityMargin = 1.0e-4f;
// Registered display names, in operation index order. Operations beyond the table get "Effect N".
constexpr const char* operationNameTable[] = {"Repeat / stutter", "Reverse slice", "Gate", "Half-time",
                                              "Tape stop",        "Filter sweep",  "Bit crush"};
constexpr const char* kindSuffix[] = {"Enable trim", "Depth trim", "Slice trim"};
constexpr const char* kindId[] = {"Enable", "Depth", "Slice"};

bool depthIsDefault(float v) noexcept { return !std::isfinite(v) || v >= 1.0f - identityMargin; }
bool sliceIsDefault(float v) noexcept { return !std::isfinite(v) || std::abs(v - 1.0f) <= identityMargin; }
} // namespace

std::string operationTrimId(std::size_t operation, TrimKind kind)
{
    return "op" + std::to_string(operation) + kindId[static_cast<std::size_t>(kind) % trimKindCount];
}

std::string operationDisplayName(std::size_t operation)
{
    if (operation < std::size(operationNameTable))
        return operationNameTable[operation];
    return "Effect " + std::to_string(operation + 1);
}

std::string operationTrimName(std::size_t operation, TrimKind kind)
{
    return operationDisplayName(operation) + ": " + kindSuffix[static_cast<std::size_t>(kind) % trimKindCount];
}

bool isIdentity(const OperationTrim& trim) noexcept
{
    return trim.enable == EnableTrim::Scene && depthIsDefault(trim.depth) && sliceIsDefault(trim.slice);
}

bool isIdentity(const OperationTrims& trims) noexcept
{
    for (const auto& t : trims)
        if (!isIdentity(t))
            return false;
    return true;
}

void applyTrims(VariationSettings& settings, const OperationTrims& trims) noexcept
{
    for (std::size_t i = 0; i < operationCount; ++i)
    {
        const auto& trim = trims[i];
        auto& op = settings.operations[i];
        if (trim.enable == EnableTrim::ForceOff)
            op.enabled = false;
        else if (trim.enable == EnableTrim::ForceOn)
            op.enabled = true;
        if (!depthIsDefault(trim.depth))
            op.depth = std::clamp(op.depth * std::clamp(trim.depth, trimDepthMin, trimDepthMax),
                                  scene::minDepth, scene::maxDepth);
        if (!sliceIsDefault(trim.slice))
            op.sliceBeats = std::clamp(op.sliceBeats * std::clamp(trim.slice, trimSliceMin, trimSliceMax),
                                       scene::minSliceBeats, scene::maxSliceBeats);
    }
}
} // namespace groovemorph::core::control
