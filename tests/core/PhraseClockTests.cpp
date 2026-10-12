#include "core/PhraseClock.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

using groovemorph::core::PhraseClock;
using groovemorph::core::PhraseGrid;
using groovemorph::core::Quantization;

namespace
{
int failures = 0;

void expectEqual(std::string_view name, long long actual, long long expected)
{
    if (actual != expected)
    {
        std::cerr << name << ": expected " << expected << ", got " << actual << '\n';
        ++failures;
    }
}

void expectInvalid(std::string_view name, double rate, const PhraseGrid& grid)
{
    if (PhraseClock::samplesPerMeterBeat(rate, grid).has_value())
    {
        std::cerr << name << ": expected invalid timing to be rejected\n";
        ++failures;
    }
}
}

int main()
{
    PhraseGrid grid;
    auto result = PhraseClock::samplesUntilBoundary(50000, 48000.0, grid,
                                                     Quantization::Beat);
    expectEqual("next beat at 120 BPM", result.value_or(-1), 22000);

    result = PhraseClock::samplesUntilBoundary(50000, 48000.0, grid,
                                                Quantization::Bar);
    expectEqual("next 4/4 bar", result.value_or(-1), 46000);

    grid.phraseBars = 2;
    result = PhraseClock::samplesUntilBoundary(100000, 48000.0, grid,
                                                Quantization::Phrase);
    expectEqual("next two-bar phrase", result.value_or(-1), 92000);

    result = PhraseClock::samplesUntilBoundary(96000, 48000.0, grid,
                                                Quantization::Bar);
    expectEqual("exact boundary", result.value_or(-1), 0);

    grid = PhraseGrid{120.0, 3, 8, 1, 0};
    result = PhraseClock::samplesUntilBoundary(10000, 48000.0, grid,
                                                Quantization::Bar);
    expectEqual("3/8 bar boundary", result.value_or(-1), 26000);

    grid = PhraseGrid{120.0, 4, 4, 1, -1000};
    result = PhraseClock::samplesUntilBoundary(0, 48000.0, grid,
                                                Quantization::Bar);
    expectEqual("nonzero sample origin", result.value_or(-1), 95000);

    grid = PhraseGrid{120.0, 4, 4, 1, 0, 0.0};
    result = PhraseClock::samplesUntilPpqBoundary(3.5, 0.0, 48000.0, grid,
                                                   Quantization::Bar);
    expectEqual("PPQ bar alignment before boundary", result.value_or(-1), 12000);
    result = PhraseClock::samplesUntilPpqBoundary(4.0, 0.0, 48000.0, grid,
                                                   Quantization::Bar);
    expectEqual("PPQ exact bar boundary", result.value_or(-1), 0);
    result = PhraseClock::samplesUntilPpqBoundary(5.5, 4.0, 48000.0, grid,
                                                   Quantization::Bar);
    expectEqual("nonzero last-bar PPQ origin", result.value_or(-1), 60000);

    grid.phraseBars = 2;
    result = PhraseClock::samplesUntilPpqBoundary(7.5, 4.0, 48000.0, grid,
                                                   Quantization::Phrase);
    expectEqual("phrase aligned to global phrase origin", result.value_or(-1), 12000);
    result = PhraseClock::samplesUntilPpqBoundary(13.0, 12.0, 48000.0, grid,
                                                   Quantization::Phrase);
    expectEqual("phrase alignment remains stable on odd bar", result.value_or(-1), 72000);

    grid.originPpq = 2.0;
    result = PhraseClock::samplesUntilPpqBoundary(5.5, 4.0, 48000.0, grid,
                                                   Quantization::Phrase);
    expectEqual("user phrase origin respected", result.value_or(-1), 108000);

    grid = PhraseGrid{90.0, 4, 4, 1, 0, 0.0};
    result = PhraseClock::samplesUntilPpqBoundary(10.0, 8.0, 48000.0, grid,
                                                   Quantization::Bar);
    expectEqual("local tempo after tempo-map change", result.value_or(-1), 64000);

    grid = PhraseGrid{120.0, 4, 4, 1, 0, 0.0};
    result = PhraseClock::samplesUntilPpqBoundary(-0.25, -4.0, 48000.0, grid,
                                                   Quantization::Beat);
    expectEqual("negative PPQ preroll", result.value_or(-1), 6000);

    result = PhraseClock::samplesUntilPpqBoundary(
        std::numeric_limits<double>::quiet_NaN(), 0.0, 48000.0, grid, Quantization::Bar);
    expectEqual("non-finite PPQ rejected", result.has_value(), false);

    grid.tempoBpm = std::numeric_limits<double>::quiet_NaN();
    expectInvalid("NaN tempo", 48000.0, grid);
    grid = PhraseGrid{301.0, 4, 4, 1, 0};
    expectInvalid("out-of-range tempo", 48000.0, grid);
    grid = PhraseGrid{120.0, 4, 3, 1, 0};
    expectInvalid("unsupported denominator", 48000.0, grid);
    grid = PhraseGrid{};
    expectInvalid("invalid sample rate", 1000.0, grid);

    if (failures == 0)
        std::cout << "PhraseClockTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
