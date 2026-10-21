#pragma once

#include "core/OperationParams.h"
#include "core/variation/CandidateBuilder.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// F08 generator history file codec (docs/specs/F08-VARIATION-HISTORY.md). Portable and JUCE free: the
// Generator tab owns the file I/O, this unit owns the text format so it can be tested with hostile input.
namespace groovemorph::core::variation
{
inline constexpr std::size_t maxHistoryEntries = 24;
inline constexpr std::size_t maxHistoryFileBytes = 1024 * 1024;
inline constexpr const char* historyHeader = "groovemorph-generator-history 2";

struct HistoryEntry
{
    VariationSettings source{};     // the scene the candidate was generated from
    GeneratorSettings settings{};   // seed, style, distance, candidate, protected ops, event generation, phrase
    std::size_t scene = 0;          // scene index the candidate was generated for
};

// Newest entry first. Only the first maxHistoryEntries entries are written.
[[nodiscard]] std::string serializeHistory(const std::vector<HistoryEntry>& entries);

// Parses serializeHistory() output. Never trusts the text: a wrong or legacy header, an oversized
// file (> maxHistoryFileBytes), a malformed entry header (stops there and keeps the earlier entries),
// a non-finite distance or an unreadable source scene is rejected. At most maxHistoryEntries entries are returned.
[[nodiscard]] std::vector<HistoryEntry> parseHistory(std::string_view text);
} // namespace groovemorph::core::variation
