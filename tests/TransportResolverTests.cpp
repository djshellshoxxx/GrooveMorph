#include "core/TransportResolver.h"

#include <iostream>
#include <limits>
#include <string_view>

using namespace groovemorph::core;

namespace
{
int failures = 0;

void expect(std::string_view name, bool condition)
{
    if (!condition)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}
} // namespace

int main()
{
    const PhraseGrid manual{100.0, 3, 4, 2, 0};

    HostTransport full{140.0, 7, 8, false, 4800};
    auto r = TransportResolver::resolve(full, manual, 0, 48000.0);
    expect("full host resolves", r.has_value());
    expect("full host source", r && r->source == TimingSource::Host && r->missingFields == 0);
    expect("full host values", r && r->grid.tempoBpm == 140.0 && r->grid.meterNumerator == 7
                                   && r->grid.meterDenominator == 8 && !r->isPlaying
                                   && r->currentSample == 4800);
    expect("manual phrase length kept", r && r->grid.phraseBars == 2);

    r = TransportResolver::resolve(HostTransport{}, manual, 1234, 48000.0);
    expect("empty host is manual", r && r->source == TimingSource::Manual);
    expect("manual uses free-run sample", r && r->currentSample == 1234 && r->isPlaying);
    expect("manual tempo/meter", r && r->grid.tempoBpm == 100.0 && r->grid.meterNumerator == 3);
    expect("all fields missing", r && r->missingFields == (MissingField::tempo | MissingField::meter
                                                            | MissingField::playState
                                                            | MissingField::position));

    HostTransport tempoOnly;
    tempoOnly.tempoBpm = 90.0;
    r = TransportResolver::resolve(tempoOnly, manual, 10, 48000.0);
    expect("tempo without position is free-run", r && r->source == TimingSource::FreeRun);
    expect("free-run uses host tempo", r && r->grid.tempoBpm == 90.0);

    HostTransport noMeter{120.0, std::nullopt, std::nullopt, true, 0};
    r = TransportResolver::resolve(noMeter, manual, 0, 48000.0);
    expect("missing meter is mixed", r && r->source == TimingSource::Mixed
                                         && r->missingFields == MissingField::meter);

    HostTransport invalid{std::numeric_limits<double>::quiet_NaN(), 4, 3, true, -5};
    r = TransportResolver::resolve(invalid, manual, 77, 48000.0);
    expect("invalid host values fall back", r && r->grid.tempoBpm == 100.0
                                                && r->grid.meterDenominator == 4
                                                && r->currentSample == 77);
    expect("invalid fields flagged missing",
           r && (r->missingFields & (MissingField::tempo | MissingField::meter | MissingField::position))
                    == (MissingField::tempo | MissingField::meter | MissingField::position));

    HostTransport tooFast{1000.0, 4, 4, true, 0};
    r = TransportResolver::resolve(tooFast, manual, 0, 48000.0);
    expect("out-of-range host tempo falls back", r && r->grid.tempoBpm == 100.0);

    expect("invalid manual grid rejected",
           !TransportResolver::resolve(full, PhraseGrid{0.0, 4, 4, 1, 0}, 0, 48000.0));
    expect("invalid sample rate rejected", !TransportResolver::resolve(full, manual, 0, 0.0));

    if (failures == 0)
        std::cout << "TransportResolverTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
