#pragma once

#include "core/scene/SceneBank.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// N02 preset model and `.gmpreset` file format. Portable: no JUCE, no file I/O.
namespace groovemorph::core::preset
{
inline constexpr std::string_view fileMagic = "groovemorph-preset";
inline constexpr int fileVersion = 1;
inline constexpr std::string_view fileExtension = ".gmpreset";

inline constexpr std::size_t maxNameLength = 48;
inline constexpr std::size_t maxCategoryLength = 24;
inline constexpr std::size_t maxAuthorLength = 48;
inline constexpr std::size_t maxTagLength = 24;
inline constexpr std::size_t maxTags = 8;
inline constexpr std::size_t maxParams = 8;
inline constexpr std::size_t maxFileBytes = 512 * 1024; // a bank at lane capacity serializes to about 350 KB (WiringAuditTests S2)
inline constexpr std::size_t maxStemLength = 48;

// Normalised (0..1) host parameter value stored with a preset.
struct PresetParam
{
    std::string id;
    float value = 0.0f;

    friend bool operator==(const PresetParam&, const PresetParam&) = default;
};

struct Preset
{
    std::string name;
    std::string category;
    std::string author;
    std::vector<std::string> tags;
    std::vector<PresetParam> params;
    std::string sceneText; // SceneSerializer text of a whole bank
    bool factory = false;  // never serialized

    friend bool operator==(const Preset&, const Preset&) = default;
};

// Host parameter ids a preset may carry (everything else is ignored).
[[nodiscard]] bool isAllowedParam(std::string_view id) noexcept;

// Printable ASCII only (others -> '_'), trimmed, truncated. Tags additionally lose commas.
[[nodiscard]] std::string sanitizeText(std::string_view text, std::size_t maxLength);
[[nodiscard]] std::string sanitizeTag(std::string_view tag);
// Safe file name stem (no separators / "..", Windows reserved names suffixed, never empty).
[[nodiscard]] std::string sanitizeFileStem(std::string_view name);
[[nodiscard]] bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept;
[[nodiscard]] std::string toLower(std::string_view text);

// Builds a preset from a bank (scene blob = canonical serialization). Fields are sanitized.
[[nodiscard]] Preset makePreset(std::string_view name, std::string_view category,
                                std::vector<std::string> tags, const scene::SceneBank& bank,
                                std::string_view author = {}, std::vector<PresetParam> params = {});

struct ParseResult
{
    bool ok = false;
    Preset preset;
    std::vector<std::string> notes; // recovery notes (accepted) or the reason (rejected, first entry)
};

[[nodiscard]] std::string serializePreset(const Preset& preset);
// Never throws for any input. Rejects on bad header/version, no scenes marker, empty name,
// unreadable scene blob or oversize input.
[[nodiscard]] ParseResult parsePreset(std::string_view text) noexcept;

// The bank described by the preset (nullopt when its scene blob is unreadable).
[[nodiscard]] std::optional<scene::SceneBank> bankOf(const Preset& preset);
// Replaces the four scenes of `current` with the preset's; keeps active scene, seed and MIDI map.
[[nodiscard]] scene::SceneBank applyToBank(const scene::SceneBank& current, const scene::SceneBank& presetBank);
// True when the four scenes (names and settings) are identical.
[[nodiscard]] bool sameScenes(const scene::SceneBank& a, const scene::SceneBank& b) noexcept;
} // namespace groovemorph::core::preset
