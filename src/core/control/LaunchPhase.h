#pragma once

#include "core/control/PhaseResolver.h"

#include <cstddef>

namespace groovemorph::core::control
{
// C04: the phase configuration of a scene request is captured when the request is made and used when
// the switch fires, so editing the scene in between affects only future launches
// (docs/specs/W3-PHASE-CAPTURE.md). Single slot: a newer request replaces the older one.
// Audio-thread safe: trivially copyable, no allocation.
class LaunchPhase final
{
public:
    void capture(std::size_t variation, const PhaseConfig& config) noexcept
    {
        held = true;
        heldVariation = variation;
        heldConfig = config;
        sanitize(heldConfig);
    }
    void clear() noexcept { held = false; }
    [[nodiscard]] bool has() const noexcept { return held; }

    // The captured config when it belongs to `variation`, else `fallback`. Always clears the slot.
    [[nodiscard]] PhaseConfig take(std::size_t variation, const PhaseConfig& fallback) noexcept
    {
        const bool match = held && heldVariation == variation;
        held = false;
        return match ? heldConfig : fallback;
    }

private:
    bool held = false;
    std::size_t heldVariation = 0;
    PhaseConfig heldConfig{};
};
} // namespace groovemorph::core::control
