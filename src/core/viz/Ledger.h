#pragma once

#include "core/TransportResolver.h"
#include "core/viz/PathResolver.h"

#include <cstdint>
#include <string>
#include <vector>

// The "Change Ledger": every number behind the pictures as plain text (docs/visual-feedback/SPEC.md
// section 9). Pure (UI thread only, allocates strings), so it is fully unit-testable and also feeds the
// accessibility text and the copy-as-text command.
namespace groovemorph::core::viz
{
// Processor status sampled by the UI (all fields come from relaxed atomics / parameters).
struct StatusSnapshot
{
    TimingSource timingSource = TimingSource::Manual;
    bool timingValid = false;
    std::uint32_t missingFields = 0; // MissingField bitmask
    bool transportPlaying = false;
    double barInPhrase = 0.0;        // [0, phraseBars)
    double beatInBar = 0.0;          // [0, meterNumerator)
    int meterNumerator = 4;
    int phraseBars = 4;
    int activeScene = 0;             // 0..3
    int queuedScene = -1;            // -1 none
    std::int64_t samplesUntilSwitch = -1; // -1 unknown
    bool previewing = false;
    double sampleRate = 48000.0;
    double historySeconds = 4.0;
    double tailSeconds = 4.003;
};

enum class BannerKind : std::uint8_t
{
    None,
    NoData,  // nothing recorded yet
    Idle,    // transport stopped: effects bypassed
    Invalid, // timing unusable
    Preview, // audition of a scene that is not committed
};

struct LedgerRow
{
    std::string label;
    std::string value;
    std::string note;
};

struct Ledger
{
    BannerKind banner = BannerKind::None;
    std::string bannerText;
    std::vector<LedgerRow> rows;
};

[[nodiscard]] Ledger buildLedger(const FrameWindow& frames, const StatusSnapshot& status);

// One line per row: "label: value (note)", preceded by the banner when present.
[[nodiscard]] std::string ledgerAsText(const Ledger& ledger);

// Helpers exposed for tests and the views.
[[nodiscard]] std::string formatDb(double linearGain);             // "-14.0 dB", "-inf dB" for ~0
[[nodiscard]] std::string sceneName(int index);                    // 0..3 -> "A".."D"
[[nodiscard]] std::string missingFieldsText(std::uint32_t mask);   // "tempo, meter, play state, position, PPQ, bar anchor"
} // namespace groovemorph::core::viz
