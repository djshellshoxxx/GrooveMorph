#include "core/scene/AuditionSession.h"

#include <utility>

namespace groovemorph::core::scene
{
void AuditionSession::begin(const SceneBank& stored, std::size_t index)
{
    variationIndex = index < variationCount ? index : variationCount - 1;
    original = stored.variation(variationIndex);
    working = original;
    active = true;
    hearStoredVariation = false;
}

void AuditionSession::setEdited(Variation v)
{
    v.name = sanitizeName(v.name);
    sanitize(v.settings);
    working = std::move(v);
}

const Variation& AuditionSession::audible() const noexcept
{
    return hearStoredVariation ? original : working;
}

SceneSnapshot AuditionSession::audibleSnapshot(const SceneBank& bank) const noexcept
{
    auto snap = bank.snapshot();
    if (active)
    {
        snap.activeIndex = variationIndex;
        snap.variations[variationIndex] = audible().settings;
    }
    return snap;
}

bool AuditionSession::commit(SceneBank& bank)
{
    if (!active)
        return false;
    bank.setVariation(variationIndex, working);
    original = working; // the committed scene is the new stored scene
    active = false;
    hearStoredVariation = false;
    return true;
}

void AuditionSession::revert()
{
    active = false;
    hearStoredVariation = false;
    working = original;
}
} // namespace groovemorph::core::scene
