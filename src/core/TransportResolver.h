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
    // Musical position in quarter notes (JUCE AudioPlayHead conventions).
    std::optional<double> ppqPosition = std::nullopt;
    std::optional<double> ppqPositionOfLastBarStart = std::nullopt;
};

// Stable parameter values: append rather than reorder.
enum class TimingMode { Automatic = 0, HostPpq = 1, SampleClock = 2 };

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
inline constexpr std::uint32_t ppq = 1u << 4;
inline constexpr std::uint32_t barAnchor = 1u << 5;
} // namespace MissingField

struct ResolvedTiming
{
    PhraseGrid grid;
    TimingMode mode = TimingMode::Automatic;
    std::int64_t currentSample = 0;
    bool isPlaying = false;
    std::optional<double> ppqPosition;
    std::optional<double> ppqPositionOfLastBarStart;
    TimingSource source = TimingSource::Manual;
    std::uint32_t missingFields = 0; // bitmask of MissingField values
    double sampleRate = 48000.0;
    // True when grid.originSample was derived from the host musical position (ppq),
    // so grid beats/bars/phrases align with host bars. Otherwise the origin is the
    // manual grid's originSample (sample 0 by default).
    bool barAlignedToHost = false;

    // Helpers for DSP. Valid for any ResolvedTiming returned by TransportResolver.
    [[nodiscard]] double samplesPerBeat() const noexcept;      // one meter beat
    [[nodiscard]] double samplesPerBar() const noexcept;
    [[nodiscard]] double samplesPerPhrase() const noexcept;
    [[nodiscard]] double positionInBeats() const noexcept;     // since grid origin
    [[nodiscard]] double positionInBars() const noexcept;
    [[nodiscard]] double positionInPhrases() const noexcept;
    [[nodiscard]] double beatInBar() const noexcept;           // [0, meterNumerator)
    [[nodiscard]] double barInPhrase() const noexcept;         // [0, phraseBars)
};

class TransportResolver final
{
public:
    // Combines host fields with manual fallbacks. Invalid host values are treated as missing.
    // freeRunSample is used as the position when the host reports none.
    // When host ppqPosition is valid the grid origin is placed at the start of host bar 1
    // (ppq 0, assuming a constant tempo/meter); ppqPositionOfLastBarStart, when consistent,
    // refines bar alignment. Phrases start on host bars 1, 1 + phraseBars, ...
    // Returns nullopt only when the manual grid is itself invalid.
    [[nodiscard]] static std::optional<ResolvedTiming> resolve(
        const HostTransport& host,
        const PhraseGrid& manual,
        std::int64_t freeRunSample,
        double sampleRate, TimingMode mode = TimingMode::Automatic) noexcept;
};
} // namespace groovemorph::core
