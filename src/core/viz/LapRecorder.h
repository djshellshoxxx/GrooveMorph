#pragma once

#include "core/trace/TraceFrame.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Per-lap summaries of what the engine actually did at each position of the phrase, built from measured
// trace frames (docs/visual-feedback/SPEC.md section 8, Phrase Rings). Fixed capacity, allocation-free.
namespace groovemorph::core::viz
{
inline constexpr std::size_t lapCells = 96; // resolution around the ring
inline constexpr std::size_t maxLaps = 8;

struct LapCell
{
    std::uint16_t frames = 0;                 // timed frames that fell into this cell
    std::array<std::uint16_t, 5> count{};     // per effect: frames in which it was doing something
    // Effect "doing something": a history stage moving the sound (lag or rate), or the gate closed.
    [[nodiscard]] std::uint8_t coverage(std::size_t effect) const noexcept
    {
        return frames == 0 ? 0 : static_cast<std::uint8_t>((static_cast<unsigned>(count[effect]) * 255u) / frames);
    }
    [[nodiscard]] bool active(std::size_t effect) const noexcept { return frames > 0 && count[effect] * 2u >= frames; }
};

struct Lap
{
    std::int64_t index = -1; // phrase number since the epoch started (floor(beat / phraseBeats))
    std::array<LapCell, lapCells> cells{};
};

struct LapDiff
{
    std::array<std::uint8_t, lapCells> mask{}; // bit per effect whose active state differs between two laps
};

[[nodiscard]] LapDiff lapDiff(const Lap& a, const Lap& b) noexcept;

class LapRecorder final
{
public:
    // Frames must be ascending; frames already consumed (sample <= last) and untimed frames are skipped.
    // A different epoch resets the recorder. phraseBeats <= 0 or non-finite is ignored.
    void feed(const trace::TraceFrame* frames, std::size_t count, double phraseBeats) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::size_t lapCount() const noexcept { return laps; }
    // 0 = newest lap (possibly still filling), 1 = the one before, ...; nullptr when out of range.
    [[nodiscard]] const Lap* lapAt(std::size_t age) const noexcept;

private:
    std::array<Lap, maxLaps> ring{};
    std::int64_t newest = -1;
    std::size_t laps = 0;
    std::uint32_t epoch = 0;
    std::int64_t lastSample = -1;
    bool started = false;
};
} // namespace groovemorph::core::viz
