#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace groovemorph::core::control
{
// C05 automatic return (subset): after a scene switch, schedule a quantized switch back
// N bars later. Audio-thread object: noexcept, no allocation.
//
// Bars are counted from the start of the bar in which the switch fired:
// arm(switchBar = floor(positionInBars), bars). The return is requested during the bar before
// the target bar boundary so the Scheduler's NextBar quantization lands it exactly on the
// boundary (sample accurate, including mid-block boundaries).
class AutoReturn final
{
public:
    void arm(double positionInBarsAtSwitch, int bars, std::size_t returnScene) noexcept;
    void disarm() noexcept { isArmed = false; requested = false; dry = false; }
    // C05 "Dry" target (docs/specs/W3-DRY-RETURN.md): same bar counting, but the boundary is not a scene
    // request; the processor drops the wet signal there. poll() never returns a scene for a Dry arming.
    void armDry(double positionInBarsAtSwitch, int bars) noexcept;
    [[nodiscard]] bool isDry() const noexcept { return isArmed && dry; }
    // Samples from `positionInBars` to the first sample at or after the Dry boundary (0 when reached).
    // nullopt when not armed for Dry or the inputs are not finite.
    [[nodiscard]] std::optional<std::int64_t> dryBoundaryOffset(double positionInBars, double samplesPerBar) const noexcept;
    [[nodiscard]] bool armed() const noexcept { return isArmed; }
    [[nodiscard]] std::optional<std::size_t> target() const noexcept;

    // Call once per block with the block-start position in bars. Returns the scene to request
    // with NextBar quantization (at most once per arming), strictly after the start of the bar
    // that precedes the return boundary. A backwards jump of more than one
    // bar before the arming bar (seek/loop) disarms.
    [[nodiscard]] std::optional<std::size_t> poll(double positionInBars) noexcept;
    // The request fired; the arming is consumed after the boundary switch is observed.
    [[nodiscard]] bool requestPending() const noexcept { return isArmed && requested; }

private:
    bool isArmed = false;
    bool requested = false;
    bool dry = false;
    double armBar = 0.0;
    double targetBar = 0.0;
    std::size_t scene = 0;
};
} // namespace groovemorph::core::control
