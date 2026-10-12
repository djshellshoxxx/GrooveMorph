#include "core/TransportResolver.h"

#include <cmath>

namespace groovemorph::core
{
std::optional<ResolvedTiming> TransportResolver::resolve(const HostTransport& host,
                                                         const PhraseGrid& manual,
                                                         std::int64_t freeRunSample,
                                                         double sampleRate) noexcept
{
    if (!PhraseClock::samplesPerMeterBeat(sampleRate, manual).has_value())
        return std::nullopt;

    ResolvedTiming result;
    result.grid = manual;
    int hostFields = 0;

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
        result.missingFields |= MissingField::playState;

    const bool hasSamplePosition = host.timeInSamples.has_value();
    if (hasSamplePosition)
        result.currentSample = *host.timeInSamples;
    else
        result.currentSample = freeRunSample;

    if (host.ppqPosition && std::isfinite(*host.ppqPosition))
        result.ppqPosition = *host.ppqPosition;
    if (host.ppqPositionOfLastBarStart
        && std::isfinite(*host.ppqPositionOfLastBarStart))
        result.ppqPositionOfLastBarStart = *host.ppqPositionOfLastBarStart;

    // A musical PPQ position is sufficient for beat/bar scheduling even when the
    // host omits an absolute sample position. currentSample remains a fallback only.
    const bool hasHostPosition = hasSamplePosition || result.ppqPosition.has_value();
    if (hasHostPosition)
        ++hostFields;
    else
        result.missingFields |= MissingField::position;

    if (!hasHostPosition)
        result.source = hostFields == 0 ? TimingSource::Manual : TimingSource::FreeRun;
    else
        result.source = result.missingFields == 0 ? TimingSource::Host : TimingSource::Mixed;

    return result;
}
} // namespace groovemorph::core
