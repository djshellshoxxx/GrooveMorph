#pragma once

#include "core/scene/SceneBank.h"

#include <cstddef>

namespace groovemorph::core::scene
{
// Audition/compare workflow (GM-FR-009). Edits go to a working copy; the persisted bank is
// only changed by an explicit commit(). Message thread only.
class AuditionSession final
{
public:
    // Starts auditioning `index` of `stored`, discarding any previous uncommitted session.
    void begin(const SceneBank& stored, std::size_t index);
    [[nodiscard]] bool isActive() const noexcept { return active; }
    [[nodiscard]] std::size_t index() const noexcept { return variationIndex; }

    [[nodiscard]] const Variation& stored() const noexcept { return original; }
    [[nodiscard]] const Variation& edited() const noexcept { return working; }
    void setEdited(Variation v); // sanitized
    [[nodiscard]] bool isDirty() const noexcept { return !(working == original); }

    // Compare A/B: false = hear edited (B), true = hear stored (A).
    void setComparingStored(bool hearStored) noexcept { hearStoredVariation = hearStored; }
    void toggleCompare() noexcept { hearStoredVariation = !hearStoredVariation; }
    [[nodiscard]] bool comparingStored() const noexcept { return hearStoredVariation; }
    [[nodiscard]] const Variation& audible() const noexcept;

    // Snapshot of `bank` with the audition variation selected and overridden by audible().
    // Returns bank.snapshot() unchanged when inactive. `bank` is not modified.
    [[nodiscard]] SceneSnapshot audibleSnapshot(const SceneBank& bank) const noexcept;

    // Writes the edited variation into `bank` and ends the session. False when inactive.
    bool commit(SceneBank& bank);
    void revert(); // ends the session, discarding edits

private:
    Variation original;
    Variation working;
    std::size_t variationIndex = 0;
    bool active = false;
    bool hearStoredVariation = false;
};
} // namespace groovemorph::core::scene
