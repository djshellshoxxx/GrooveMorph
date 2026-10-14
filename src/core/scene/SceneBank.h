#pragma once

#include "core/OperationParams.h"
#include "core/scene/MidiNoteMap.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace groovemorph::core::scene
{
inline constexpr std::size_t maxVariationNameLength = 32;
inline constexpr float minDepth = 0.0f;
inline constexpr float maxDepth = 1.0f;
inline constexpr float minSliceBeats = 1.0f / 16.0f;
inline constexpr float maxSliceBeats = 4.0f;
inline constexpr std::uint64_t defaultSeed = 0x47524F4F56454D31ull; // "GROOVEM1"

// Clamp to the defined range; non-finite values become the OperationSettings default.
// Returns true when the input was changed.
bool sanitize(OperationSettings& settings) noexcept;
bool sanitize(VariationSettings& settings) noexcept;
// Printable ASCII only (others -> '_'), truncated to maxVariationNameLength.
[[nodiscard]] std::string sanitizeName(std::string_view name);

[[nodiscard]] bool sameSettings(const OperationSettings& a, const OperationSettings& b) noexcept;
[[nodiscard]] bool sameSettings(const VariationSettings& a, const VariationSettings& b) noexcept;

struct Variation
{
    std::string name;
    VariationSettings settings{};

    friend bool operator==(const Variation& a, const Variation& b) noexcept
    {
        return a.name == b.name && sameSettings(a.settings, b.settings);
    }
};

// Cheap, trivially copyable audio-thread view of a bank (no names / strings).
struct SceneSnapshot
{
    std::array<VariationSettings, variationCount> variations{};
    std::size_t activeIndex = 0;
    std::uint64_t seed = defaultSeed;
    MidiNoteMap midiMap{};
};

// Message-thread model of the four variations (GM-FR-002), selection and MIDI map.
class SceneBank final
{
public:
    SceneBank(); // musically distinct defaults (see defaultVariation)
    [[nodiscard]] static Variation defaultVariation(std::size_t index);

    [[nodiscard]] const Variation& variation(std::size_t index) const noexcept; // index clamped
    void setVariation(std::size_t index, Variation v); // sanitized; out-of-range index ignored
    void setVariationName(std::size_t index, std::string_view name);
    void setVariationSettings(std::size_t index, const VariationSettings& settings);
    // Copy only effect settings between scenes. Preserve names, MIDI mapping,
    // active scene and seed. False on invalid, self or semantically equal copy.
    [[nodiscard]] bool copyVariationSettings(std::size_t source, std::size_t destination) noexcept;

    // Duplicate an entire scene (effect settings, placement/lane/permissions AND name) into
    // another slot. Preserves MIDI mapping, active scene and seed. False on invalid, self or
    // identical copy.
    [[nodiscard]] bool duplicateVariation(std::size_t source, std::size_t destination) noexcept;
    // Restore the factory scene (settings and name) for one slot.
    void resetVariation(std::size_t index) noexcept;

    [[nodiscard]] std::size_t activeIndex() const noexcept { return active; }
    void setActiveIndex(std::size_t index) noexcept; // clamped

    [[nodiscard]] std::uint64_t seed() const noexcept { return seedValue; }
    void setSeed(std::uint64_t value) noexcept { seedValue = value; }

    [[nodiscard]] const MidiNoteMap& midiNoteMap() const noexcept { return midiMap; }
    void setMidiNoteMap(const MidiNoteMap& map) noexcept; // notes clamped to 0..127

    [[nodiscard]] SceneSnapshot snapshot() const noexcept;

    friend bool operator==(const SceneBank&, const SceneBank&) = default;

private:
    std::array<Variation, variationCount> variations;
    std::size_t active = 0;
    std::uint64_t seedValue = defaultSeed;
    MidiNoteMap midiMap{};
};
} // namespace groovemorph::core::scene
