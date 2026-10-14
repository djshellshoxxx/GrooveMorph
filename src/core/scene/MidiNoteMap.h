#pragma once

#include "core/OperationParams.h"

#include <array>
#include <cstddef>
#include <optional>

namespace groovemorph::core::scene
{
enum class NoteOffPolicy
{
    Ignore,           // default: note-off never reverts the scene (GM-FR-008)
    ReturnToPrevious, // releasing the triggering note returns to the variation active before it
};

inline constexpr int midiNoteMin = 0;
inline constexpr int midiNoteMax = 127;

// Trivially copyable note -> variation map. Default C3=60..D#3=63 -> variations 0..3.
struct MidiNoteMap
{
    std::array<int, variationCount> notes{60, 61, 62, 63};
    NoteOffPolicy noteOffPolicy = NoteOffPolicy::Ignore;

    // First variation mapped to the note, or nullopt when unmapped/out of range.
    [[nodiscard]] std::optional<std::size_t> variationForNote(int note) const noexcept;
    // First valid MIDI note assigned to more than one variation, or nullopt.
    // Diagnostics only: preserves the existing first-mapped-variation behavior.
    [[nodiscard]] std::optional<int> firstConflictingNote() const noexcept;

    friend bool operator==(const MidiNoteMap&, const MidiNoteMap&) = default;
};

// Audio-thread note trigger state machine (noexcept, no allocation).
class MidiTriggerState final
{
public:
    // Returns the variation to select/queue for a note-on, or nullopt when unmapped.
    [[nodiscard]] std::optional<std::size_t> noteOn(
        const MidiNoteMap& map, int note, std::size_t currentActive) noexcept;

    // Returns the variation to return to, only for ReturnToPrevious when the released note is
    // the one that triggered the current variation. Otherwise nullopt (no change).
    [[nodiscard]] std::optional<std::size_t> noteOff(const MidiNoteMap& map, int note) noexcept;

    void reset() noexcept;

private:
    int heldNote = -1;
    std::size_t previousIndex = 0;
};
} // namespace groovemorph::core::scene
