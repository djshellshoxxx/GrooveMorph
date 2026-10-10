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
};

class PhraseClock final
{
public:
    // Returns one beat in the time-signature denominator as a sample duration.
    [[nodiscard]] static std::optional<long double> samplesPerMeterBeat(
        double sampleRate, const PhraseGrid& grid) noexcept;

    // Returns samples until the selected boundary. An exact boundary returns zero.
    [[nodiscard]] static std::optional<std::int64_t> samplesUntilBoundary(
        std::int64_t currentSample,
        double sampleRate,
        const PhraseGrid& grid,
        Quantization quantization) noexcept;
};
} // namespace groovemorph::core
