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
};

enum class TimingSource
{
    Host,        // all timing fields came from the host
    Mixed,       // some fields came from the host, the rest from manual settings
    Manual,      // host provided no usable timing; manual tempo/meter in use
    FreeRun      // no host position; internal sample counter drives the grid
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
    bool isPlaying = false;
    TimingSource source = TimingSource::Manual;
    std::uint32_t missingFields = 0; // bitmask of MissingField values
};

class TransportResolver final
{
public:
    // Combines host fields with manual fallbacks. Invalid host values are treated as missing.
    // freeRunSample is used as the position when the host reports none.
    // Returns nullopt only when the manual grid is itself invalid.
    [[nodiscard]] static std::optional<ResolvedTiming> resolve(
        const HostTransport& host,
        const PhraseGrid& manual,
        std::int64_t freeRunSample,
        double sampleRate) noexcept;
};
} // namespace groovemorph::core
