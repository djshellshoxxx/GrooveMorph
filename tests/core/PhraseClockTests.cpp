#include "core/PhraseClock.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
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
    expectEqual("nonzero grid origin", result.value_or(-1), 95000);

    // Position helpers relative to the grid origin.
    const auto near = [](std::optional<double> v, double expected) {
        return v && std::abs(*v - expected) < 1.0e-9;
    };
    grid = PhraseGrid{120.0, 3, 4, 2, 24000};
    if (!near(PhraseClock::positionInBeats(96000, 48000.0, grid), 3.0)
        || !near(PhraseClock::positionInBars(96000, 48000.0, grid), 1.0)
        || !near(PhraseClock::positionInPhrases(96000, 48000.0, grid), 0.5)
        || !near(PhraseClock::positionInBeats(0, 48000.0, grid), -1.0))
    {
        std::cerr << "position helpers: unexpected value\n";
        ++failures;
    }
    if (PhraseClock::positionInBeats(0, 0.0, grid).has_value())
    {
        std::cerr << "position helpers: invalid rate accepted\n";
        ++failures;
    }

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
