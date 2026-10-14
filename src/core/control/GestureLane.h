#pragma once

#include <array>
#include <string>
#include <string_view>

namespace groovemorph::core::control
{
// F11 gesture lane: one phrase of recorded control movement (normalised 0..1 parameter value),
// stored in fixed bins and played back by phrase position. Message-thread object.
class GestureLane final
{
public:
    static constexpr int binCount = 256;

    void clear() noexcept;
    // phraseFraction in [0,1); value clamped to 0..1.
    void record(double phraseFraction, float value) noexcept;
    [[nodiscard]] bool hasData() const noexcept { return filled > 0; }
    [[nodiscard]] int filledBins() const noexcept { return filled; }
    // Interpolated value at the phrase position (wrapping between the nearest recorded bins);
    // negative when nothing has been recorded.
    [[nodiscard]] float sample(double phraseFraction) const noexcept;

    // Text form for project state: space-separated "bin:value" pairs of the recorded bins.
    [[nodiscard]] std::string serialize() const;
    // Replaces the lane from serialize() output. Malformed or out-of-range items are skipped (never
    // trusted); returns the number of bins restored. Input longer than 16 KiB is ignored.
    std::size_t deserialize(std::string_view text) noexcept;

private:
    std::array<float, binCount> values{};
    std::array<bool, binCount> has{};
    int filled = 0;
};
} // namespace groovemorph::core::control
