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
    double originPpq = 0.0;
};

class PhraseClock final
{
public:
    [[nodiscard]] static std::optional<long double> samplesPerMeterBeat(
        double sampleRate, const PhraseGrid& grid) noexcept;

    // Sample-domain fallback, anchored to originSample. Exact boundaries return zero.
    [[nodiscard]] static std::optional<std::int64_t> samplesUntilBoundary(
        std::int64_t currentSample,
        double sampleRate,
        const PhraseGrid& grid,
        Quantization quantization) noexcept;

    // PPQ uses quarter-note units. Beat/bar sync uses the host's last bar start;
    // phrase sync uses the configured phrase origin so multi-bar periods remain stable.
    [[nodiscard]] static std::optional<std::int64_t> samplesUntilPpqBoundary(
        double currentPpq,
        double barStartPpq,
        double sampleRate,
        const PhraseGrid& grid,
        Quantization quantization) noexcept;
};
} // namespace groovemorph::core
