#pragma once

#include "core/scene/SceneBank.h"

#include <string>
#include <string_view>
#include <vector>

namespace groovemorph::core::scene
{
// Format: first line "groovemorph-scenes v1", then "key=value" lines (GM-FR-010).
inline constexpr std::string_view sceneFormatMagic = "groovemorph-scenes";
inline constexpr int sceneFormatVersion = 1;

enum class LoadStatus
{
    Ok,        // parsed cleanly
    Recovered, // loaded with some fields clamped/defaulted (see messages)
    Defaulted, // header missing/unknown version: factory defaults returned
};

struct LoadReport
{
    LoadStatus status = LoadStatus::Ok;
    std::vector<std::string> messages; // human-readable recovery notes
};

struct LoadResult
{
    SceneBank bank;
    LoadReport report;
};

class SceneSerializer final
{
public:
    [[nodiscard]] static std::string serialize(const SceneBank& bank);
    // Never throws for malformed input; unknown keys are ignored (not reported as recovery).
    [[nodiscard]] static LoadResult deserialize(std::string_view text) noexcept;
};
} // namespace groovemorph::core::scene
