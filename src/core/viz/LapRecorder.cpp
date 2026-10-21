#include "core/viz/LapRecorder.h"

#include "core/OperationParams.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core::viz
{
namespace
{
// Which effects are doing something in this frame (bit per OperationType).
std::uint8_t activeMask(const trace::TraceFrame& f) noexcept
{
    std::uint8_t m = 0;
    const struct { OperationType op; int slot; } history[] = {{OperationType::Repeat, 0}, {OperationType::ReverseSlice, 1},
                                                              {OperationType::HalfTime, 2}, {OperationType::TapeStop, 3}};
    for (const auto& h : history)
    {
        const auto o = static_cast<std::size_t>(h.op);
        if (f.amount[o] >= 0.5f && (f.lagSamples[h.slot] > 0.5f || f.rate[h.slot] != 1.0f))
            m = static_cast<std::uint8_t>(m | (1u << o));
    }
    const auto gate = static_cast<std::size_t>(OperationType::Gate);
    if (f.amount[gate] >= 0.5f && f.gateGain < 0.25f)
        m = static_cast<std::uint8_t>(m | (1u << gate));
    for (const OperationType tonal : {OperationType::FilterSweep, OperationType::BitCrush})
    {
        const auto o = static_cast<std::size_t>(tonal);
        if (f.amount[o] >= 0.5f)
            m = static_cast<std::uint8_t>(m | (1u << o));
    }
    return m;
}
} // namespace

LapDiff lapDiff(const Lap& a, const Lap& b) noexcept
{
    LapDiff d;
    for (std::size_t c = 0; c < lapCells; ++c)
    {
        std::uint8_t m = 0;
        for (std::size_t e = 0; e < operationCount; ++e)
            if (a.cells[c].active(e) != b.cells[c].active(e))
                m = static_cast<std::uint8_t>(m | (1u << e));
        d.mask[c] = m;
    }
    return d;
}

void LapRecorder::reset() noexcept
{
    ring = {};
    newest = -1;
    laps = 0;
    lastSample = -1;
    started = false;
}

const Lap* LapRecorder::lapAt(std::size_t age) const noexcept
{
    if (age >= laps || age >= maxLaps)
        return nullptr;
    const std::int64_t index = newest - static_cast<std::int64_t>(age);
    const Lap& lap = ring[static_cast<std::size_t>(index % static_cast<std::int64_t>(maxLaps))];
    return lap.index == index ? &lap : nullptr;
}

void LapRecorder::feed(const trace::TraceFrame* frames, std::size_t count, double phraseBeats) noexcept
{
    if (frames == nullptr || !std::isfinite(phraseBeats) || !(phraseBeats > 0.0))
        return;
    for (std::size_t i = 0; i < count; ++i)
    {
        const trace::TraceFrame& f = frames[i];
        if (started && f.epoch != epoch)
            reset();
        if (!started)
        {
            epoch = f.epoch;
            started = true;
        }
        if (f.sample <= lastSample)
            continue;
        lastSample = f.sample;
        if ((f.flags & trace::TimingOk) == 0 || !std::isfinite(f.beat) || f.beat < 0.0)
            continue;
        const double lapPos = f.beat / phraseBeats;
        const std::int64_t index = static_cast<std::int64_t>(std::floor(lapPos));
        if (index < newest) // time went backwards without a new epoch: ignore the frame
            continue;
        Lap& lap = ring[static_cast<std::size_t>(index % static_cast<std::int64_t>(maxLaps))];
        if (lap.index != index)
        {
            lap = Lap{};
            lap.index = index;
            if (index != newest)
            {
                newest = index;
                laps = std::min<std::size_t>(maxLaps, laps + 1);
            }
        }
        const double frac = lapPos - static_cast<double>(index);
        const std::size_t cell = std::min<std::size_t>(lapCells - 1, static_cast<std::size_t>(frac * static_cast<double>(lapCells)));
        LapCell& c = lap.cells[cell];
        if (c.frames < 0xFFFF)
            ++c.frames;
        const std::uint8_t m = activeMask(f);
        for (std::size_t e = 0; e < operationCount; ++e)
            if ((m >> e) & 1u)
                ++c.count[e];
    }
}
} // namespace groovemorph::core::viz
