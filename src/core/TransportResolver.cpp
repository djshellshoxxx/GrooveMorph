#include "core/TransportResolver.h"

#include <cmath>

namespace groovemorph::core
{
namespace
{
double positiveModulo(double value, double modulus) noexcept
{
    const auto r = std::fmod(value, modulus);
    const auto wrapped = r < 0.0 ? r + modulus : r;
    return wrapped >= modulus ? 0.0 : wrapped;
}

// Returns the bar-aligned origin derived from host ppq, or nullopt if ppq is unusable.
std::optional<std::int64_t> hostOrigin(const HostTransport& host,
                                       const PhraseGrid& grid,
                                       std::int64_t currentSample,
                                       double sampleRate) noexcept
{
    constexpr double maxPpq = 1.0e9;
    if (!host.ppqPosition || !std::isfinite(*host.ppqPosition)
        || std::abs(*host.ppqPosition) > maxPpq)
        return std::nullopt;
    const auto samplesPerBeat = PhraseClock::samplesPerMeterBeat(sampleRate, grid);
    if (!samplesPerBeat)
        return std::nullopt;

    const long double quartersPerBeat = 4.0L / static_cast<long double>(grid.meterDenominator);
    const long double samplesPerQuarter = *samplesPerBeat / quartersPerBeat;
    const long double quartersPerBar = quartersPerBeat * grid.meterNumerator;
    const long double ppq = *host.ppqPosition;

    long double samplesBack = ppq * samplesPerQuarter; // distance back to ppq 0
    if (host.ppqPositionOfLastBarStart && std::isfinite(*host.ppqPositionOfLastBarStart))
    {
        const long double barStart = *host.ppqPositionOfLastBarStart;
        const long double sinceBar = ppq - barStart;
        constexpr long double tolerance = 1.0e-6L;
        if (std::abs(barStart) <= maxPpq && sinceBar >= -tolerance
            && sinceBar < quartersPerBar + tolerance)
        {
            const auto barIndex = std::round(static_cast<double>(barStart / quartersPerBar));
            const auto barInPhrase = positiveModulo(barIndex, grid.phraseBars);
            samplesBack = (sinceBar + static_cast<long double>(barInPhrase) * quartersPerBar)
                        * samplesPerQuarter;
        }
    }
    const auto origin = std::round(static_cast<long double>(currentSample) - samplesBack);
    if (!std::isfinite(origin) || std::abs(origin) > 9.0e18L)
        return std::nullopt;
    return static_cast<std::int64_t>(origin);
}
} // namespace

double ResolvedTiming::samplesPerBeat() const noexcept
{
    const auto v = PhraseClock::samplesPerMeterBeat(sampleRate, grid);
    return v ? static_cast<double>(*v) : 0.0;
}

double ResolvedTiming::samplesPerBar() const noexcept
{
    return samplesPerBeat() * grid.meterNumerator;
}

double ResolvedTiming::samplesPerPhrase() const noexcept
{
    return samplesPerBar() * grid.phraseBars;
}

double ResolvedTiming::positionInBeats() const noexcept
{
    return PhraseClock::positionInBeats(currentSample, sampleRate, grid).value_or(0.0);
}

double ResolvedTiming::positionInBars() const noexcept
{
    return PhraseClock::positionInBars(currentSample, sampleRate, grid).value_or(0.0);
}

double ResolvedTiming::positionInPhrases() const noexcept
{
    return PhraseClock::positionInPhrases(currentSample, sampleRate, grid).value_or(0.0);
}

double ResolvedTiming::beatInBar() const noexcept
{
    return positiveModulo(positionInBeats(), grid.meterNumerator);
}

double ResolvedTiming::barInPhrase() const noexcept
{
    return positiveModulo(positionInBars(), grid.phraseBars);
}


std::optional<ResolvedTiming> TransportResolver::resolve(const HostTransport& host,
                                                         const PhraseGrid& manual,
                                                         std::int64_t freeRunSample,
                                                         double sampleRate) noexcept
{
    if (!PhraseClock::samplesPerMeterBeat(sampleRate, manual).has_value())
        return std::nullopt;

    ResolvedTiming result;
    result.grid = manual;
    result.sampleRate = sampleRate;
    int hostFields = 0;

    // Each host field is accepted only if PhraseClock accepts the grid with it applied.
    auto candidate = result.grid;
    if (host.tempoBpm)
        candidate.tempoBpm = *host.tempoBpm;
    if (host.tempoBpm && PhraseClock::samplesPerMeterBeat(sampleRate, candidate))
    {
        result.grid = candidate;
        ++hostFields;
    }
    else
        result.missingFields |= MissingField::tempo;

    candidate = result.grid;
    if (host.meterNumerator && host.meterDenominator)
    {
        candidate.meterNumerator = *host.meterNumerator;
        candidate.meterDenominator = *host.meterDenominator;
    }
    if (host.meterNumerator && host.meterDenominator
        && PhraseClock::samplesPerMeterBeat(sampleRate, candidate))
    {
        result.grid = candidate;
        ++hostFields;
    }
    else
        result.missingFields |= MissingField::meter;

    if (host.isPlaying)
    {
        result.isPlaying = *host.isPlaying;
        ++hostFields;
    }
    else
    {
        result.isPlaying = false; // missing play state must not assert host playback
        result.missingFields |= MissingField::playState;
    }

    const bool hasPosition = host.timeInSamples.has_value(); // signed sample positions permit preroll
    if (hasPosition)
    {
        result.currentSample = *host.timeInSamples;
        ++hostFields;
    }
    else
    {
        result.currentSample = freeRunSample;
        result.missingFields |= MissingField::position;
    }

    if (const auto origin = hostOrigin(host, result.grid, result.currentSample, sampleRate))
    {
        result.grid.originSample = *origin;
        result.barAlignedToHost = true;
    }

    if (!hasPosition)
        result.source = hostFields == 0 ? TimingSource::Manual : TimingSource::FreeRun;
    else
        result.source = result.missingFields == 0 ? TimingSource::Host : TimingSource::Mixed;

    return result;
}
} // namespace groovemorph::core
