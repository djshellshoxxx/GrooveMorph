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

std::optional<int> MidiNoteMap::firstConflictingNote() const noexcept
{
    // This tiny model-only diagnostic is independent of JUCE, host automation,
    // and the audio-thread dispatch policy. Ignore invalid MIDI note numbers.
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const auto note = notes[i];
        if (note < midiNoteMin || note > midiNoteMax)
            continue;
        for (std::size_t j = i + 1; j < notes.size(); ++j)
            if (notes[j] == note)
                return note;
    }
    return std::nullopt;
}

std::optional<std::size_t> MidiNoteMap::ownerOf(int note, std::size_t except) const noexcept
{
    if (note < midiNoteMin || note > midiNoteMax)
        return std::nullopt;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (i != except && notes[i] == note)
            return i;
    return std::nullopt;
}

NoteAssignPlan MidiNoteMap::planAssign(std::size_t index, int note) const noexcept
{
    NoteAssignPlan plan;
    if (index >= notes.size() || note < midiNoteMin || note > midiNoteMax)
        return plan;
    if (notes[index] == note)
    {
        plan.kind = NoteAssignPlan::Kind::Unchanged;
        return plan;
    }
    if (const auto owner = ownerOf(note, index))
    {
        plan.kind = NoteAssignPlan::Kind::Conflict;
        plan.owner = *owner;
        plan.ownerNewNote = notes[index];
        return plan;
    }
    plan.kind = NoteAssignPlan::Kind::Free;
    return plan;
}

bool MidiNoteMap::assign(std::size_t index, int note, ConflictChoice choice) noexcept
{
    const auto plan = planAssign(index, note);
    switch (plan.kind)
    {
    case NoteAssignPlan::Kind::Free:
        notes[index] = note;
        return true;
    case NoteAssignPlan::Kind::Conflict:
        if (choice != ConflictChoice::Swap)
            return false;
        // Only the first owner is swapped (a legacy duplicate of the note, if any, is left as saved).
        notes[plan.owner] = plan.ownerNewNote;
        notes[index] = note;
        return true;
    default:
        return false;
    }
}

std::optional<std::size_t> MidiTriggerState::releaseAll(const MidiNoteMap& map) noexcept
{
    // MIDI All Notes Off (CC 123) / All Sound Off (CC 120): hosts and controllers send these instead
    // of individual note-offs, so a held trigger note would otherwise stay held until reset().
    return noteOff(map, heldNote);
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
