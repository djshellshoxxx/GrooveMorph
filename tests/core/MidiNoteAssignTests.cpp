#include "core/scene/MidiNoteMap.h"
#include "core/scene/SceneBank.h"

#include <iostream>
#include <string_view>

using namespace groovemorph::core::scene;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}
} // namespace

int main() {
    // A04-01: ownerOf reports who owns a note, ignoring the asking variation; invalid notes have no owner.
    {
        MidiNoteMap map; // 60 61 62 63
        expect("A04-01 owner", map.ownerOf(61) == std::size_t{1});
        expect("A04-01 owner except self", !map.ownerOf(61, 1).has_value());
        expect("A04-01 unmapped", !map.ownerOf(70).has_value());
        expect("A04-01 invalid note", !map.ownerOf(-1).has_value() && !map.ownerOf(128).has_value());
    }
    // A04-02: planAssign classifies the request without changing the map.
    {
        MidiNoteMap map;
        const auto before = map;
        expect("A04-02 unchanged", map.planAssign(0, 60).kind == NoteAssignPlan::Kind::Unchanged);
        expect("A04-02 free", map.planAssign(0, 72).kind == NoteAssignPlan::Kind::Free);
        const auto conflict = map.planAssign(0, 62);
        expect("A04-02 conflict", conflict.kind == NoteAssignPlan::Kind::Conflict && conflict.owner == 2
                                   && conflict.ownerNewNote == 60);
        expect("A04-02 invalid", map.planAssign(4, 60).kind == NoteAssignPlan::Kind::Invalid
                                  && map.planAssign(0, 200).kind == NoteAssignPlan::Kind::Invalid
                                  && map.planAssign(0, -3).kind == NoteAssignPlan::Kind::Invalid);
        expect("A04-02 planning is const", map == before);
    }
    // A04-03: assign on a free note works; on a conflict only Swap changes the map, and a swap never
    // leaves a duplicate; Cancel and invalid requests leave the map untouched.
    {
        MidiNoteMap map;
        expect("A04-03 free assign", map.assign(0, 72) && map.notes[0] == 72);
        MidiNoteMap swap;
        expect("A04-03 cancel keeps map", !swap.assign(0, 62, ConflictChoice::Cancel) && swap == MidiNoteMap{});
        expect("A04-03 default is cancel", !swap.assign(0, 62) && swap == MidiNoteMap{});
        expect("A04-03 swap", swap.assign(0, 62, ConflictChoice::Swap) && swap.notes[0] == 62 && swap.notes[2] == 60
                               && swap.notes[1] == 61 && swap.notes[3] == 63);
        expect("A04-03 swap result has no conflict", !swap.firstConflictingNote().has_value());
        MidiNoteMap same;
        expect("A04-03 same note is not a change", !same.assign(1, 61, ConflictChoice::Swap) && same == MidiNoteMap{});
        MidiNoteMap bad;
        expect("A04-03 invalid refused", !bad.assign(9, 60, ConflictChoice::Swap) && !bad.assign(0, 128, ConflictChoice::Swap)
                                          && bad == MidiNoteMap{});
    }
    // A04-04: legacy maps with duplicates stay valid: first match wins for dispatch, and a swap only
    // involves the first owner (no new duplicate is created).
    {
        MidiNoteMap legacy;
        legacy.notes = {60, 60, 62, 63};
        expect("A04-04 legacy dispatch is first-match", legacy.variationForNote(60) == std::size_t{0});
        expect("A04-04 legacy conflict still reported", legacy.firstConflictingNote() == 60);
        expect("A04-04 legacy map equal after bank round trip", [&] {
            SceneBank bank;
            bank.setMidiNoteMap(legacy);
            return bank.midiNoteMap() == legacy;
        }());
        auto swapped = legacy;
        expect("A04-04 swap takes first owner only", swapped.assign(2, 60, ConflictChoice::Swap) && swapped.notes[2] == 60
                                                      && swapped.notes[0] == 62 && swapped.notes[1] == 60);
        auto fixed = legacy;
        expect("A04-04 moving off a duplicate is a free assign", fixed.assign(1, 80) && !fixed.firstConflictingNote().has_value());
    }
    // A04-05: releaseAll acts as the note-off of the held note (panic CC 120/123).
    {
        MidiNoteMap map;
        map.noteOffPolicy = NoteOffPolicy::ReturnToPrevious;
        MidiTriggerState state;
        expect("A04-05 nothing held", !state.releaseAll(map).has_value() && !state.isHeld());
        expect("A04-05 note on", state.noteOn(map, 61, 2) == std::size_t{1} && state.isHeld());
        expect("A04-05 panic returns to previous", state.releaseAll(map) == std::size_t{2} && !state.isHeld());
        expect("A04-05 panic twice is a no-op", !state.releaseAll(map).has_value());
        // The late note-off of the same note after a panic must not trigger a second return.
        expect("A04-05 late note-off ignored", !state.noteOff(map, 61).has_value());
        // Ignore policy: the hold is cleared but no scene change is requested.
        MidiNoteMap ignore;
        MidiTriggerState other;
        (void) other.noteOn(ignore, 62, 0);
        expect("A04-05 ignore policy clears hold silently", !other.releaseAll(ignore).has_value() && !other.isHeld());
        // After a panic the previous scene is captured afresh by the next note-on.
        expect("A04-05 fresh capture", state.noteOn(map, 63, 3) == std::size_t{3} && state.noteOff(map, 63) == std::size_t{3});
    }

    if (failures == 0) std::cout << "MidiNoteAssignTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
