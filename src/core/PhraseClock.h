#pragma once

#include <cstdint>
#include <optional>

namespace groovemorph::core
{
enum class Quantization
{
    Beat,
    Bar,
    Phrase
};

struct PhraseGrid
{
    double tempoBpm = 120.0;
    int meterNumerator = 4;
    int meterDenominator = 4;
    int phraseBars = 1;
    std::int64_t originSample = 0;
    double originPpq = 0.0; // explicit musical phrase anchor in quarter-note units
};

class PhraseClock final
{
public:
    // Returns one beat in the time-signature denominator as a sample duration.
    [[nodiscard]] static std::optional<long double> samplesPerMeterBeat(
        double sampleRate, const PhraseGrid& grid) noexcept;

    // Returns samples until the selected boundary. Boundaries are rounded to the nearest
    // sample; a boundary whose rounded sample equals currentSample returns zero.
    [[nodiscard]] static std::optional<std::int64_t> samplesUntilBoundary(
        std::int64_t currentSample,
        double sampleRate,
        const PhraseGrid& grid,
        Quantization quantization) noexcept;

    // Musical boundary at the current host tempo; updated each callback rather than
    // counting down stale samples through tempo-map changes.
    [[nodiscard]] static std::optional<std::int64_t> samplesUntilPpqBoundary(
        double currentPpq, std::optional<double> barStartPpq,
        double sampleRate, const PhraseGrid& grid, Quantization quantization) noexcept;

    // Position relative to grid.originSample, in meter beats / bars / phrases.
    // Negative before the origin. nullopt when the grid or sample rate is invalid.
    [[nodiscard]] static std::optional<double> positionInBeats(
        std::int64_t currentSample, double sampleRate, const PhraseGrid& grid) noexcept;
    [[nodiscard]] static std::optional<double> positionInBars(
        std::int64_t currentSample, double sampleRate, const PhraseGrid& grid) noexcept;
    [[nodiscard]] static std::optional<double> positionInPhrases(
        std::int64_t currentSample, double sampleRate, const PhraseGrid& grid) noexcept;
};
} // namespace groovemorph::core
