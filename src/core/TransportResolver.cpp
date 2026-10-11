#include "core/TransportResolver.h"

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
        result.isPlaying = true; // free-run keeps the grid advancing
        result.missingFields |= MissingField::playState;
    }

    const bool hasPosition = host.timeInSamples.has_value() && *host.timeInSamples >= 0;
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

    if (!hasPosition)
        result.source = hostFields == 0 ? TimingSource::Manual : TimingSource::FreeRun;
    else
        result.source = result.missingFields == 0 ? TimingSource::Host : TimingSource::Mixed;

    return result;
}
} // namespace groovemorph::core
