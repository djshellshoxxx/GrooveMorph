#include "core/scene/EditCoalescing.h"

#include "core/scene/SceneBank.h"

namespace groovemorph::core::scene
{
namespace
{
// True when reverting only the field selected by `field` makes `after` equal to `before`.
template <class Field>
bool onlyThisFieldDiffers(const VariationSettings& before, const VariationSettings& after, Field field) noexcept
{
    if (field(before) == field(after))
        return false;
    auto reverted = after;
    field(reverted) = field(before);
    return sameSettings(reverted, before);
}
} // namespace

std::uint32_t singleChangedNumericField(const VariationSettings& before, const VariationSettings& after) noexcept
{
    if (sameSettings(before, after))
        return 0;
    for (std::size_t i = 0; i < operationCount; ++i)
    {
        if (onlyThisFieldDiffers(before, after, [i](auto& s) -> auto& { return s.operations[i].depth; }))
            return static_cast<std::uint32_t>(1 + 2 * i);
        if (onlyThisFieldDiffers(before, after, [i](auto& s) -> auto& { return s.operations[i].sliceBeats; }))
            return static_cast<std::uint32_t>(2 + 2 * i);
    }
    // Fixed ids from 100 up, independent of operationCount so they never collide with the rows above.
    if (onlyThisFieldDiffers(before, after, [](auto& s) -> auto& { return s.control.placement.startPhase; })) return 101;
    if (onlyThisFieldDiffers(before, after, [](auto& s) -> auto& { return s.control.placement.spanPhase; })) return 102;
    if (onlyThisFieldDiffers(before, after, [](auto& s) -> auto& { return s.control.placement.every; })) return 103;
    if (onlyThisFieldDiffers(before, after, [](auto& s) -> auto& { return s.control.placement.cadenceOffset; })) return 104;
    if (onlyThisFieldDiffers(before, after, [](auto& s) -> auto& { return s.control.placement.originTicks; })) return 105;
    if (onlyThisFieldDiffers(before, after, [](auto& s) -> auto& { return s.control.placement.subdivision; })) return 106;
    if (onlyThisFieldDiffers(before, after, [](auto& s) -> auto& { return s.control.placement.downbeatProtectBeats; })) return 107;
    if (onlyThisFieldDiffers(before, after, [](auto& s) -> auto& { return s.control.activity.maxConcurrent; })) return 110;
    if (onlyThisFieldDiffers(before, after, [](auto& s) -> auto& { return s.control.activity.minSpacingBeats; })) return 111;
    if (onlyThisFieldDiffers(before, after, [](auto& s) -> auto& { return s.control.activity.maxCoverage; })) return 112;
    if (onlyThisFieldDiffers(before, after, [](auto& s) -> auto& { return s.control.phase.offsetBeats; })) return 120;
    return 0;
}

std::uint64_t coalesceKey(std::size_t sceneIndex, const VariationSettings& before,
                          const VariationSettings& after) noexcept
{
    const auto field = singleChangedNumericField(before, after);
    if (field == 0)
        return 0;
    return (static_cast<std::uint64_t>(sceneIndex + 1) << 32) | field;
}
} // namespace groovemorph::core::scene
