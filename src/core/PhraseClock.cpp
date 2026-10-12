#include "PhraseClock.h"

#include <algorithm>
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
std::optional<long double> samplesToBoundary(long double position,
                                              long double interval,
                                              long double samplesPerBeat) noexcept
{
    if (!std::isfinite(position) || !std::isfinite(interval) || interval <= 0.0L
        || !std::isfinite(samplesPerBeat) || samplesPerBeat <= 0.0L)
        return std::nullopt;

    constexpr long double boundaryTolerance = 1.0e-12L;
    const auto boundaryNumber = std::ceil(position / interval - boundaryTolerance);
    const auto delta = (boundaryNumber * interval - position) * samplesPerBeat;
    if (!std::isfinite(delta) || delta < -1.0L
        || delta > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return std::max(0.0L, delta);
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

    // Each boundary lands on the sample nearest its exact position (floor(x + 0.5)), and the
    // first boundary whose rounded sample is >= currentSample is returned. This keeps the
    // result consistent across arbitrary block starts: a boundary is never skipped.
    const auto interval = *samplesPerBeat * *beatCount;
    const auto origin = static_cast<long double>(grid.originSample);
    const auto current = static_cast<long double>(currentSample);
    constexpr long double boundaryTolerance = 1.0e-12L;
    auto boundaryNumber = std::ceil((current - origin - 0.5L) / interval - boundaryTolerance);
    auto target = std::floor(origin + boundaryNumber * interval + 0.5L);
    if (target < current) // guard against precision loss at the rounding edge
        target = std::floor(origin + (boundaryNumber + 1.0L) * interval + 0.5L);
    const auto delta = target - current;

    if (!std::isfinite(delta) || delta < 0.0L
        || delta > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return static_cast<std::int64_t>(delta);
}

std::optional<std::int64_t> PhraseClock::samplesUntilPpqBoundary(
    double currentPpq,
    std::optional<double> barStartPpq,
    double sampleRate,
    const PhraseGrid& grid,
    Quantization quantization) noexcept
{
    if (!std::isfinite(currentPpq)
        || (quantization == Quantization::Phrase && !std::isfinite(grid.originPpq)))
        return std::nullopt;

    if ((quantization == Quantization::Beat || quantization == Quantization::Bar)
        && (!barStartPpq || !std::isfinite(*barStartPpq)))
        return std::nullopt;

    const auto samplesPerMeterBeat = PhraseClock::samplesPerMeterBeat(sampleRate, grid);
    const auto beatCount = beatsPerBoundary(grid, quantization);
    if (!samplesPerMeterBeat || !beatCount)
        return std::nullopt;

    const auto quarterNotesPerMeterBeat = 4.0L / grid.meterDenominator;
    const auto intervalPpq = *beatCount * quarterNotesPerMeterBeat;
    const auto anchorPpq = quantization == Quantization::Phrase
        ? static_cast<long double>(grid.originPpq)
        : static_cast<long double>(*barStartPpq);
    const auto relativePpq = static_cast<long double>(currentPpq) - anchorPpq;
    const auto samplesPerPpq = *samplesPerMeterBeat / quarterNotesPerMeterBeat;
    const auto delta = samplesToBoundary(relativePpq, intervalPpq, samplesPerPpq);
    if (!delta)
        return std::nullopt;

    const auto rounded = std::round(*delta);
    if (rounded > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return static_cast<std::int64_t>(rounded);
}
std::optional<double> PhraseClock::positionInBeats(
    std::int64_t currentSample, double sampleRate, const PhraseGrid& grid) noexcept
{
    const auto samplesPerBeat = samplesPerMeterBeat(sampleRate, grid);
    if (!samplesPerBeat)
        return std::nullopt;
    const auto relative = static_cast<long double>(currentSample)
        - static_cast<long double>(grid.originSample);
    return static_cast<double>(relative / *samplesPerBeat);
}

std::optional<double> PhraseClock::positionInBars(
    std::int64_t currentSample, double sampleRate, const PhraseGrid& grid) noexcept
{
    const auto beats = positionInBeats(currentSample, sampleRate, grid);
    if (!beats)
        return std::nullopt;
    return *beats / static_cast<double>(grid.meterNumerator);
}

std::optional<double> PhraseClock::positionInPhrases(
    std::int64_t currentSample, double sampleRate, const PhraseGrid& grid) noexcept
{
    const auto bars = positionInBars(currentSample, sampleRate, grid);
    if (!bars)
        return std::nullopt;
    return *bars / static_cast<double>(grid.phraseBars);
}
} // namespace groovemorph::core
