#pragma once

#include <string>
#include <string_view>

namespace groovemorph::core::printnaming
{
inline constexpr int kMaxListed = 20; // prints kept in the UI list

struct Timestamp
{
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
};

struct ParsedName
{
    std::string scene;
    double bpm = 0.0;
    int bars = 0;
    bool valid = false;
};

// Keeps ASCII letters, digits and '-'. Spaces and '_' become '-'; everything else is dropped. Runs of '-'
// collapse, the ends are trimmed, the result is cut to maxLength, an empty result becomes "Scene" and Windows
// reserved device names get a trailing '-'.
[[nodiscard]] std::string sanitizeComponent(std::string_view text, std::size_t maxLength = 32);
// Rounded to 0.1: "120" for whole values, "123.5" otherwise; invalid (non-finite or <= 0) gives "0".
[[nodiscard]] std::string formatBpm(double bpm);
// YYYYMMDD-HHMMSS
[[nodiscard]] std::string formatTimestamp(const Timestamp& ts);
// GrooveMorph_<scene>_<bpm>bpm_<bars>bars_<timestamp>.wav
[[nodiscard]] std::string buildPrintFileName(std::string_view scene, double bpm, int bars, const Timestamp& ts);
// "x.wav" -> "x-2.wav" (n >= 2); n < 2 returns the name unchanged.
[[nodiscard]] std::string withCollisionSuffix(std::string_view name, int n);
[[nodiscard]] bool isPrintFileName(std::string_view name);
// Parses from the right, so a scene containing '_' (never produced by this app) still works.
[[nodiscard]] ParsedName parsePrintFileName(std::string_view name);
} // namespace groovemorph::core::printnaming
