#include "core/TransportResolver.h"

#include <cmath>
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

    // Musical position (ppq) gives a bar/phrase-aligned origin.
    const auto near = [](double a, double b) { return std::abs(a - b) < 1.0e-9; };
    const PhraseGrid twoBar{120.0, 4, 4, 2, 0};
    HostTransport ppqHost{120.0, 4, 4, true, 1000000};
    ppqHost.ppqPosition = 10.5;
    ppqHost.ppqPositionOfLastBarStart = 8.0; // host bar 3, first bar of a phrase
    r = TransportResolver::resolve(ppqHost, twoBar, 0, 48000.0);
    expect("ppq aligned flag", r && r->barAlignedToHost && r->source == TimingSource::Host);
    expect("ppq origin at phrase start", r && r->grid.originSample == 1000000 - 60000);
    expect("samples per beat", r && near(r->samplesPerBeat(), 24000.0)
                                   && near(r->samplesPerBar(), 96000.0)
                                   && near(r->samplesPerPhrase(), 192000.0));
    expect("position helpers", r && near(r->positionInBeats(), 2.5) && near(r->positionInBars(), 0.625)
                                   && near(r->beatInBar(), 2.5) && near(r->barInPhrase(), 0.625)
                                   && near(r->positionInPhrases(), 0.3125));

    ppqHost.ppqPosition = 6.5;
    ppqHost.ppqPositionOfLastBarStart = 4.0; // host bar 2, second bar of a phrase
    r = TransportResolver::resolve(ppqHost, twoBar, 0, 48000.0);
    expect("ppq origin second phrase bar", r && r->grid.originSample == 1000000 - 156000
                                               && near(r->barInPhrase(), 1.625));

    ppqHost.ppqPositionOfLastBarStart = std::nullopt;
    r = TransportResolver::resolve(ppqHost, twoBar, 0, 48000.0);
    expect("ppq only origin", r && r->barAlignedToHost && r->grid.originSample == 1000000 - 156000);

    ppqHost.ppqPositionOfLastBarStart = 9.0; // inconsistent: after current position
    r = TransportResolver::resolve(ppqHost, twoBar, 0, 48000.0);
    expect("inconsistent bar start ignored", r && r->grid.originSample == 1000000 - 156000);

    HostTransport sixEight{120.0, 6, 8, true, 500000};
    sixEight.ppqPosition = 3.0;
    sixEight.ppqPositionOfLastBarStart = 3.0; // host bar 2 of 6/8 (3 quarters per bar)
    r = TransportResolver::resolve(sixEight, twoBar, 0, 48000.0);
    expect("6/8 samples per beat", r && near(r->samplesPerBeat(), 12000.0));
    expect("6/8 phrase origin", r && r->grid.originSample == 500000 - 72000
                                    && near(r->barInPhrase(), 1.0) && near(r->beatInBar(), 0.0));

    HostTransport nanPpq{120.0, 4, 4, true, 1000};
    nanPpq.ppqPosition = std::numeric_limits<double>::quiet_NaN();
    r = TransportResolver::resolve(nanPpq, twoBar, 0, 48000.0);
    expect("invalid ppq keeps manual origin", r && !r->barAlignedToHost && r->grid.originSample == 0);

    HostTransport preRoll{120.0, 4, 4, true, 0};
    preRoll.ppqPosition = -1.0; // one quarter of pre-roll before bar 1
    r = TransportResolver::resolve(preRoll, twoBar, 0, 48000.0);
    expect("pre-roll origin after current", r && r->grid.originSample == 24000
                                                && near(r->positionInBeats(), -1.0)
                                                && near(r->beatInBar(), 3.0));

    if (failures == 0)
        std::cout << "TransportResolverTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
