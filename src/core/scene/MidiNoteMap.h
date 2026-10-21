#pragma once

#include "core/OperationParams.h"

#include <array>
#include <cstddef>
#include <cstdint>
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

// A04: how a requested note assignment relates to the existing map.
struct NoteAssignPlan
{
    enum class Kind : std::uint8_t
    {
        Invalid,   // index or note out of range
        Unchanged, // the variation already has this note
        Free,      // nobody else uses the note
        Conflict,  // another variation owns the note
    };
    Kind kind = Kind::Invalid;
    std::size_t owner = 0;  // Conflict: the variation that currently owns the note
    int ownerNewNote = -1;  // Conflict: the note the owner receives on a swap
};
enum class ConflictChoice : std::uint8_t { Cancel, Swap };

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

    // First variation other than `except` that is mapped to the note (A04), or nullopt.
    [[nodiscard]] std::optional<std::size_t> ownerOf(int note, std::size_t except = variationCount) const noexcept;
    // What assigning `note` to variation `index` would do. Conflict names the current owner and the
    // note the owner would receive if the two swap (the note `index` gives up).
    [[nodiscard]] NoteAssignPlan planAssign(std::size_t index, int note) const noexcept;
    // Applies the assignment. Free/Unchanged: sets the note. Conflict: Swap exchanges the two notes
    // (the map stays duplicate-free if it was); Cancel changes nothing. Returns true when the map was
    // changed. Invalid index/note never changes anything.
    bool assign(std::size_t index, int note, ConflictChoice choice = ConflictChoice::Cancel) noexcept;

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

    // Releases the held note as if its note-off had arrived (panic messages). Same return contract
    // as noteOff(); nothing held means nullopt.
    [[nodiscard]] std::optional<std::size_t> releaseAll(const MidiNoteMap& map) noexcept;

    [[nodiscard]] bool isHeld() const noexcept { return heldNote >= 0; }
    void reset() noexcept;

private:
    int heldNote = -1;
    std::size_t previousIndex = 0;
};
} // namespace groovemorph::core::scene
