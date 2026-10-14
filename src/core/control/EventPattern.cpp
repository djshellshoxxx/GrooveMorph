#include "core/control/EventPattern.h"

#include <cmath>
#include <limits>

namespace groovemorph::core::control
{
namespace
{
bool finiteIn(float v, float lo, float hi) noexcept
{
    return std::isfinite(v) && v >= lo && v <= hi;
}

bool validEvent(const PhraseEvent& e, std::int64_t P) noexcept
{
    return e.id != 0
        && static_cast<std::size_t>(e.operation) < operationCount
        && e.durationTicks >= 1 && e.durationTicks <= P
        && e.onsetTicks >= 0 && e.onsetTicks < P
        && finiteIn(e.depth, 0.0f, 1.0f)
        && finiteIn(e.sliceBeats, 1.0f / 16.0f, 4.0f)
        && e.priority <= maxPriority
        && (!e.preserveCycleAnchor || (e.cycleAnchorTicks >= 0 && e.cycleAnchorTicks < P));
}

bool circularOverlap(const PhraseEvent& a, const PhraseEvent& b, std::int64_t P) noexcept
{
    return positiveModulo(b.onsetTicks - a.onsetTicks, P) < a.durationTicks
        || positiveModulo(a.onsetTicks - b.onsetTicks, P) < b.durationTicks;
}
} // namespace

std::optional<std::int64_t> meterBeatTicks(int denominator) noexcept
{
    if (denominator < 1 || denominator > 32 || (denominator & (denominator - 1)) != 0)
        return std::nullopt;
    return ppqTicks * 4 / denominator;
}

std::optional<std::int64_t> phraseTicks(int phraseBars, int numerator, int denominator) noexcept
{
    const auto beat = meterBeatTicks(denominator);
    if (!beat || phraseBars < 1 || phraseBars > 32 || numerator < 1 || numerator > 32)
        return std::nullopt;
    const std::int64_t total = static_cast<std::int64_t>(phraseBars) * numerator * *beat;
    if (total <= 0 || total > std::numeric_limits<std::int64_t>::max() / 2)
        return std::nullopt;
    return total;
}

std::optional<std::int64_t> gridTicks(std::int64_t beatTicks, int subdivision) noexcept
{
    switch (subdivision)
    {
    case 1: case 2: case 3: case 4: case 6: case 8: case 12: case 16:
        break;
    default:
        return std::nullopt;
    }
    if (beatTicks <= 0 || beatTicks % subdivision != 0)
        return std::nullopt;
    return beatTicks / subdivision;
}

std::int64_t positiveModulo(std::int64_t value, std::int64_t modulus) noexcept
{
    const std::int64_t r = value % modulus;
    return r < 0 ? r + modulus : r;
}

PatternStatus validatePattern(const EventPattern& pattern, std::int64_t P) noexcept
{
    if (P < 1)
        return PatternStatus::Invalid;
    if (pattern.count > maxEvents)
        return PatternStatus::Capacity;
    if (pattern.kind == PatternKind::LegacyUniform)
        return pattern.count == 0 ? PatternStatus::Valid : PatternStatus::Invalid;

    const std::size_t n = pattern.count;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (!validEvent(pattern.events[i], P))
            return PatternStatus::Invalid;
        for (std::size_t j = 0; j < i; ++j)
            if (pattern.events[j].id == pattern.events[i].id)
                return PatternStatus::Invalid;
    }
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j)
            if (pattern.events[i].operation == pattern.events[j].operation
                && circularOverlap(pattern.events[i], pattern.events[j], P))
                return PatternStatus::Overlap;
    return PatternStatus::Valid;
}
} // namespace groovemorph::core::control
