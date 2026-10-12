#pragma once

#include "core/PhraseClock.h"

#include <cstdint>
#include <optional>

namespace groovemorph::core
{
// Host-provided transport fields; each is absent when the host does not report it.
struct HostTransport
{
    std::optional<double> tempoBpm;
    std::optional<int> meterNumerator;
    std::optional<int> meterDenominator;
    std::optional<bool> isPlaying;
    std::optional<std::int64_t> timeInSamples;
    std::optional<double> ppqPosition;
    std::optional<double> ppqPositionOfLastBarStart;
};

enum class TimingSource
{
    Host,
    Mixed,
    Manual,
    FreeRun
};

namespace MissingField
{
inline constexpr std::uint32_t tempo = 1u << 0;
inline constexpr std::uint32_t meter = 1u << 1;
inline constexpr std::uint32_t playState = 1u << 2;
inline constexpr std::uint32_t position = 1u << 3;
} // namespace MissingField

struct ResolvedTiming
{
    PhraseGrid grid;
    std::int64_t currentSample = 0;
    bool isPlaying = false; // Unknown host state fails closed; free-run position still advances independently.
    std::optional<double> ppqPosition;
    std::optional<double> ppqPositionOfLastBarStart;
    TimingSource source = TimingSource::Manual;
    std::uint32_t missingFields = 0;
};

class TransportResolver final
{
public:
    // Combines valid host fields with manual fallbacks. Negative sample positions are valid
    // preroll positions. Missing play state is reported and never inferred as "playing".
    [[nodiscard]] static std::optional<ResolvedTiming> resolve(
        const HostTransport& host,
        const PhraseGrid& manual,
        std::int64_t freeRunSample,
        double sampleRate) noexcept;
};
} // namespace groovemorph::core
