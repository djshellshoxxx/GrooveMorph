#include "PhraseClock.h"

#include <cmath>
#include <limits>

namespace groovemorph::core
{
namespace
{
bool isSupportedDenominator(int denominator) noexcept
{
    return denominator == 1 || denominator == 2 || denominator == 4
        || denominator == 8 || denominator == 16;
}

std::optional<long double> beatsPerBoundary(const PhraseGrid& grid,
                                             Quantization quantization) noexcept
{
    switch (quantization)
    {
        case Quantization::Beat:
            return 1.0L;
        case Quantization::Bar:
            return static_cast<long double>(grid.meterNumerator);
        case Quantization::Phrase:
            return static_cast<long double>(grid.meterNumerator)
                 * static_cast<long double>(grid.phraseBars);
    }
    return std::nullopt;
}
} // namespace

std::optional<long double> PhraseClock::samplesPerMeterBeat(
    double sampleRate, const PhraseGrid& grid) noexcept
{
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 192000.0
        || !std::isfinite(grid.tempoBpm) || grid.tempoBpm < 20.0 || grid.tempoBpm > 300.0
        || grid.meterNumerator < 1 || grid.meterNumerator > 32
        || !isSupportedDenominator(grid.meterDenominator)
        || grid.phraseBars < 1 || grid.phraseBars > 16)
        return std::nullopt;

    const auto samples = static_cast<long double>(sampleRate) * 60.0L
        / static_cast<long double>(grid.tempoBpm)
        * 4.0L / static_cast<long double>(grid.meterDenominator);
    if (!std::isfinite(samples) || samples <= 0.0L)
        return std::nullopt;
    return samples;
}

std::optional<std::int64_t> PhraseClock::samplesUntilBoundary(
    std::int64_t currentSample,
    double sampleRate,
    const PhraseGrid& grid,
    Quantization quantization) noexcept
{
    const auto samplesPerBeat = samplesPerMeterBeat(sampleRate, grid);
    const auto beatCount = beatsPerBoundary(grid, quantization);
    if (!samplesPerBeat || !beatCount)
        return std::nullopt;

    const auto interval = *samplesPerBeat * *beatCount;
    const auto relative = static_cast<long double>(currentSample)
        - static_cast<long double>(grid.originSample);
    const auto positionInIntervals = relative / interval;
    constexpr long double boundaryTolerance = 1.0e-12L;
    const auto boundaryNumber = std::ceil(positionInIntervals - boundaryTolerance);
    const auto target = static_cast<long double>(grid.originSample)
        + boundaryNumber * interval;
    const auto delta = target - static_cast<long double>(currentSample);

    if (!std::isfinite(delta) || delta < -1.0L
        || delta > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    if (delta <= 0.0L)
        return std::int64_t{0};

    const auto roundedDelta = std::round(delta);
    if (roundedDelta > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return static_cast<std::int64_t>(roundedDelta);
}
} // namespace groovemorph::core
