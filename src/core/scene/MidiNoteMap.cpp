#include "core/scene/MidiNoteMap.h"

namespace groovemorph::core::scene
{
std::optional<std::size_t> MidiNoteMap::variationForNote(int note) const noexcept
{
    if (note < midiNoteMin || note > midiNoteMax)
        return std::nullopt;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (notes[i] == note)
            return i;
    return std::nullopt;
}

std::optional<std::size_t> MidiTriggerState::noteOn(
    const MidiNoteMap& map, int note, std::size_t currentActive) noexcept
{
    const auto mapped = map.variationForNote(note);
    if (!mapped)
        return std::nullopt;
    // Retriggering while a mapped note is held keeps the original "previous" scene.
    if (heldNote < 0)
        previousIndex = currentActive < variationCount ? currentActive : 0;
    heldNote = note;
    return mapped;
}

std::optional<std::size_t> MidiTriggerState::noteOff(const MidiNoteMap& map, int note) noexcept
{
    if (heldNote < 0 || note != heldNote)
        return std::nullopt;
    heldNote = -1;
    if (map.noteOffPolicy != NoteOffPolicy::ReturnToPrevious)
        return std::nullopt;
    return previousIndex;
}

void MidiTriggerState::reset() noexcept
{
    heldNote = -1;
    previousIndex = 0;
}
} // namespace groovemorph::core::scene
