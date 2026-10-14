#include "core/scene/SceneBank.h"
#include <type_traits>

#include <cmath>
#include <iostream>
#include <string_view>

using namespace groovemorph::core;
using namespace groovemorph::core::scene;

namespace
{
int failures = 0;

void expect(std::string_view name, bool condition)
{
    if (!condition)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}
} // namespace

int main()
{
    SceneBank bank;
    expect("four variations", variationCount == 4);
    for (std::size_t i = 0; i < variationCount; ++i)
        for (std::size_t j = i + 1; j < variationCount; ++j)
            expect("defaults distinct", !sameSettings(bank.variation(i).settings,
                                                      bank.variation(j).settings)
                                            && bank.variation(i).name != bank.variation(j).name);
    expect("A repeat", bank.variation(0).settings[OperationType::Repeat].enabled);
    expect("B gate", bank.variation(1).settings[OperationType::Gate].enabled);
    expect("C reverse+halftime", bank.variation(2).settings[OperationType::ReverseSlice].enabled
                                     && bank.variation(2).settings[OperationType::HalfTime].enabled);
    expect("D tapestop", bank.variation(3).settings[OperationType::TapeStop].enabled);
    expect("default seed", bank.seed() == defaultSeed && bank.activeIndex() == 0);

    // Isolation: editing one variation does not touch others.
    const SceneBank before = bank;
    auto s = bank.variation(1).settings;
    s[OperationType::Gate].depth = 0.3f;
    bank.setVariationSettings(1, s);
    expect("edit applied", bank.variation(1).settings[OperationType::Gate].depth == 0.3f);
    for (std::size_t i : {0u, 2u, 3u})
        expect("others untouched", bank.variation(i) == before.variation(i));
    expect("bank differs", !(bank == before));

    // Clamping
    s[OperationType::Gate].depth = 5.0f;
    s[OperationType::Gate].sliceBeats = std::nanf("");
    bank.setVariationSettings(1, s);
    expect("depth clamped", bank.variation(1).settings[OperationType::Gate].depth == 1.0f);
    expect("nan -> default", bank.variation(1).settings[OperationType::Gate].sliceBeats == 0.25f);
    bank.setActiveIndex(99);
    expect("active clamped", bank.activeIndex() == variationCount - 1);
    bank.setVariationName(0, std::string(100, 'x') + "\n");
    expect("name bounded", bank.variation(0).name.size() == maxVariationNameLength);
    bank.setVariationName(0, "a\tb");
    expect("name sanitized", bank.variation(0).name == "a_b");
    expect("oob index safe", bank.variation(42) == bank.variation(3));

    // Snapshot
    bank.setSeed(1234);
    const SceneSnapshot snap = bank.snapshot();
    expect("snapshot trivially copyable", std::is_trivially_copyable_v<SceneSnapshot>);
    expect("snapshot fields", snap.seed == 1234 && snap.activeIndex == 3
                                  && sameSettings(snap.variations[1], bank.variation(1).settings));

    // MIDI map
    MidiNoteMap map;
    expect("default map has no MIDI collisions", !map.firstConflictingNote());
    MidiNoteMap conflicting = map;
    conflicting.notes = {60, 60, 62, 63};
    expect("detect colliding MIDI note", conflicting.firstConflictingNote() == 60);
    conflicting.notes = {60, 61, 62, 60};
    expect("detect nonadjacent collision", conflicting.firstConflictingNote() == 60);
    conflicting.notes = {60, 60, 62, 62};
    expect("report first collision deterministically", conflicting.firstConflictingNote() == 60);
    conflicting.notes = {-1, -1, 62, 63};
    expect("ignore out-of-range note collisions", !conflicting.firstConflictingNote());
    conflicting.notes = {60, 61, 62, 63};
    expect("clearing collision removes warning", !conflicting.firstConflictingNote());
    expect("default map", map.variationForNote(60) == 0u && map.variationForNote(63) == 3u);
    expect("unmapped", !map.variationForNote(64) && !map.variationForNote(-1)
                           && !map.variationForNote(200));
    expect("default noteoff ignore", map.noteOffPolicy == NoteOffPolicy::Ignore);
    MidiTriggerState trig;
    auto on = trig.noteOn(map, 62, 0);
    expect("note-on maps", on == 2u);
    expect("note-off ignore", !trig.noteOff(map, 62));
    map.noteOffPolicy = NoteOffPolicy::ReturnToPrevious;
    on = trig.noteOn(map, 61, 2);
    expect("note-on 61", on == 1u);
    expect("other note-off no change", !trig.noteOff(map, 60));
    expect("note-off returns", trig.noteOff(map, 61) == 2u);
    expect("unmapped note-on", !trig.noteOn(map, 10, 1));
    map.notes = {36, 38, 40, 41};
    map.notes[0] = 500;
    bank.setMidiNoteMap(map);
    expect("map clamped", bank.midiNoteMap().notes[0] == 127
                              && bank.midiNoteMap().variationForNote(38) == 1u);

    if (failures == 0)
        std::cout << "SceneBankTests passed\n";
    return failures == 0 ? 0 : 1;
}
