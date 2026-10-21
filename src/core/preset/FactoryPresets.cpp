#include "core/preset/FactoryPresets.h"

#include "core/control/SceneControl.h"

#include <array>
#include <cassert>
#include <initializer_list>
#include <string>
#include <vector>
#include <utility>

namespace groovemorph::core::preset
{
namespace
{
using OT = OperationType;
using scene::Variation;
using control::LaneEvent;
using control::ticksPerBeat;

struct Op
{
    OT type;
    float depth;
    float slice;
};

// One scene: name plus its enabled operations; `shape` may tweak placement/lane/limits.
Variation scene(const char* name, std::initializer_list<Op> ops, void (*shape)(VariationSettings&) = nullptr)
{
    Variation v;
    v.name = name;
    for (const auto& op : ops)
        v.settings[op.type] = OperationSettings{true, op.depth, op.slice};
    if (shape != nullptr)
        shape(v.settings);
    return v;
}

void lastBar(VariationSettings& s)
{
    s.control.placement.enabled = true;
    s.control.placement.window = control::WindowMode::LastBar;
}

void everyFourthPhrase(VariationSettings& s)
{
    s.control.placement.enabled = true;
    s.control.placement.every = 4;
    s.control.placement.cadenceOffset = 3;
}

void lastBarEveryOther(VariationSettings& s)
{
    s.control.placement.enabled = true;
    s.control.placement.window = control::WindowMode::LastBar;
    s.control.placement.every = 2;
    s.control.placement.cadenceOffset = 1;
}

void protectDownbeat(VariationSettings& s)
{
    s.control.placement.downbeatProtectBeats = 0.25f;
}

void protectDownbeatHalf(VariationSettings& s)
{
    s.control.placement.downbeatProtectBeats = 0.5f;
}

// Explicit one-beat lane events inside the first three beats (valid for any meter/phrase length).
void addEvents(VariationSettings& s, std::initializer_list<LaneEvent> events)
{
    s.control.lane.explicitEvents = true;
    for (const auto& e : events)
        (void)control::addEvent(s.control.lane, e, 3 * ticksPerBeat);
}

LaneEvent event(OT op, int onsetSixteenths, int lengthSixteenths, float depth, float slice = 0.0f)
{
    LaneEvent e;
    e.operation = static_cast<std::uint8_t>(op);
    e.onsetTicks = onsetSixteenths * (ticksPerBeat / 4);
    e.durationTicks = lengthSixteenths * (ticksPerBeat / 4);
    e.depth = depth;
    e.sliceBeats = slice;
    return e;
}

void pickupEvents(VariationSettings& s)
{
    addEvents(s, {event(OT::Repeat, 8, 4, 1.0f, 0.125f), event(OT::Gate, 0, 4, 0.8f)});
}

void snareRollEvents(VariationSettings& s)
{
    addEvents(s, {event(OT::Repeat, 0, 4, 1.0f, 0.25f), event(OT::Repeat, 4, 4, 1.0f, 0.125f),
                  event(OT::Repeat, 8, 4, 1.0f, 0.0625f)});
}

void backbeatTapeEvents(VariationSettings& s)
{
    addEvents(s, {event(OT::TapeStop, 8, 4, 0.7f, 1.0f), event(OT::Gate, 0, 4, 0.5f, 0.25f)});
}

void polyGate(VariationSettings& s)
{
    s.control.lane.loopBeats[static_cast<std::size_t>(OT::Gate)] = 3;
    s.control.lane.loopBeats[static_cast<std::size_t>(OT::Repeat)] = 5;
    addEvents(s, {event(OT::Gate, 0, 8, 1.0f, 0.25f), event(OT::Repeat, 0, 4, 0.7f, 0.25f)});
}

void limitTwo(VariationSettings& s)
{
    s.control.activity.enabled = true;
    s.control.activity.maxConcurrent = 2;
}

void lockRepeatDepth(VariationSettings& s)
{
    s.control.permissions.operationLocks[static_cast<std::size_t>(OT::Repeat)] = control::lock::enable;
}

struct Def
{
    const char* name;
    const char* category;
    std::vector<const char*> tags;
    std::vector<Variation> scenes; // exactly four
    std::vector<PresetParam> params;
};

void add(std::vector<Preset>& table, const Def& def)
{
    scene::SceneBank bank;
    for (std::size_t i = 0; i < 4; ++i)
        bank.setVariation(i, def.scenes[i]);
    auto p = makePreset(def.name, def.category, std::vector<std::string>(def.tags.begin(), def.tags.end()), bank,
                        "GrooveMorph", std::vector<PresetParam>(def.params.begin(), def.params.end()));
    p.factory = true;
    table.push_back(std::move(p));
}

std::vector<Preset> build()
{
    std::vector<Preset> t;
    t.reserve(32);
    // ---- Init: the factory default bank, always first so a host selecting program 0 is harmless ----
    {
        auto init = makePreset("Init", "Init", {"init", "default"}, scene::SceneBank{}, "GrooveMorph");
        init.factory = true;
        t.push_back(std::move(init));
    }
    // ---- Stutter ----
    add(t, {"Sixteenth Stutter", "Stutter", {"stutter", "tight", "drums"},
            {scene("A Sixteenth", {{OT::Repeat, 1.0f, 0.25f}}), scene("B Eighth", {{OT::Repeat, 0.9f, 0.5f}}),
             scene("C Quarter", {{OT::Repeat, 0.8f, 1.0f}}), scene("D Fast 1/16", {{OT::Repeat, 1.0f, 0.0625f}})}, {}});
    add(t, {"Triplet Stutter", "Stutter", {"stutter", "triplet", "swing"},
            {scene("A Triplet 8th", {{OT::Repeat, 1.0f, 1.0f / 3.0f}}),
             scene("B Triplet 16th", {{OT::Repeat, 0.9f, 1.0f / 6.0f}}),
             scene("C Triplet Quarter", {{OT::Repeat, 0.8f, 2.0f / 3.0f}}),
             scene("D Trip + Gate", {{OT::Repeat, 0.8f, 1.0f / 3.0f}, {OT::Gate, 0.6f, 1.0f / 6.0f}})}, {}});
    add(t, {"Stutter Ladder", "Stutter", {"stutter", "build", "step"},
            {scene("A Half Beat", {{OT::Repeat, 1.0f, 0.5f}}), scene("B Quarter Beat", {{OT::Repeat, 1.0f, 0.25f}}),
             scene("C Eighth Beat", {{OT::Repeat, 1.0f, 0.125f}}), scene("D Sixteenth Beat", {{OT::Repeat, 1.0f, 0.0625f}})}, {}});
    add(t, {"Gated Stutter", "Stutter", {"stutter", "gate", "choppy"},
            {scene("A Repeat Gate", {{OT::Repeat, 1.0f, 0.25f}, {OT::Gate, 0.7f, 0.125f}}),
             scene("B Wide Gate", {{OT::Repeat, 0.9f, 0.5f}, {OT::Gate, 0.6f, 0.25f}}),
             scene("C Tight", {{OT::Repeat, 1.0f, 0.125f}, {OT::Gate, 0.8f, 0.0625f}}),
             scene("D Dry Gate", {{OT::Gate, 0.8f, 0.125f}})}, {}});
    add(t, {"Reverse Stutter", "Stutter", {"reverse", "stutter", "glitch"},
            {scene("A Back 1/4", {{OT::ReverseSlice, 1.0f, 0.25f}, {OT::Repeat, 0.6f, 0.25f}}),
             scene("B Back 1/2", {{OT::ReverseSlice, 1.0f, 0.5f}, {OT::Repeat, 0.5f, 0.5f}}),
             scene("C Back 1", {{OT::ReverseSlice, 1.0f, 1.0f}}),
             scene("D Back 1/8", {{OT::ReverseSlice, 0.9f, 0.125f}, {OT::Repeat, 0.7f, 0.125f}})}, {}});
    // ---- Halftime ----
    add(t, {"Half Time Drop", "Halftime", {"halftime", "heavy", "drop"},
            {scene("A Half Time", {{OT::HalfTime, 1.0f, 1.0f}}), scene("B Half + Gate", {{OT::HalfTime, 1.0f, 1.0f}, {OT::Gate, 0.5f, 0.5f}}),
             scene("C Half 2 Beats", {{OT::HalfTime, 1.0f, 2.0f}}), scene("D Half Light", {{OT::HalfTime, 0.5f, 1.0f}})}, {}});
    add(t, {"Slow Roll", "Halftime", {"halftime", "tape", "outro"},
            {scene("A Half Time", {{OT::HalfTime, 1.0f, 1.0f}}),
             scene("B Half + Tape End", {{OT::HalfTime, 1.0f, 1.0f}, {OT::TapeStop, 0.8f, 1.0f}}, lastBar),
             scene("C Half 4 Beats", {{OT::HalfTime, 1.0f, 4.0f}}),
             scene("D Half + Reverse", {{OT::HalfTime, 0.8f, 2.0f}, {OT::ReverseSlice, 0.6f, 1.0f}})}, {}});
    add(t, {"Half Time Reverse", "Halftime", {"halftime", "reverse", "dark"},
            {scene("A Half Rev", {{OT::HalfTime, 1.0f, 1.0f}, {OT::ReverseSlice, 0.7f, 2.0f}}),
             scene("B Rev Bar End", {{OT::HalfTime, 0.8f, 1.0f}, {OT::ReverseSlice, 1.0f, 1.0f}}, lastBar),
             scene("C Half Only", {{OT::HalfTime, 1.0f, 2.0f}}), scene("D Rev Only", {{OT::ReverseSlice, 0.8f, 1.0f}})}, {}});
    add(t, {"Halftime Breather", "Halftime", {"halftime", "gentle", "pad"},
            {scene("A Soft Half", {{OT::HalfTime, 0.4f, 1.0f}}), scene("B Soft Half 2", {{OT::HalfTime, 0.5f, 2.0f}}),
             scene("C Soft Half 4", {{OT::HalfTime, 0.6f, 4.0f}}), scene("D Half + Soft Gate", {{OT::HalfTime, 0.5f, 2.0f}, {OT::Gate, 0.3f, 1.0f}})},
            {{"dryWet", 0.7f}}});
    // ---- Gate Chops ----
    add(t, {"Trance Gate Eighth", "Gate Chops", {"gate", "trance", "eighth"},
            {scene("A 1/8 Gate", {{OT::Gate, 1.0f, 0.5f}}), scene("B 1/16 Gate", {{OT::Gate, 1.0f, 0.25f}}),
             scene("C 1/4 Gate", {{OT::Gate, 0.9f, 1.0f}}), scene("D Soft 1/8", {{OT::Gate, 0.5f, 0.5f}})}, {}});
    add(t, {"Choppy Sixteenth", "Gate Chops", {"gate", "choppy", "fast"},
            {scene("A 1/16", {{OT::Gate, 1.0f, 0.25f}}), scene("B 1/32", {{OT::Gate, 0.9f, 0.125f}}),
             scene("C 1/64", {{OT::Gate, 0.8f, 0.0625f}}), scene("D 1/16 Soft", {{OT::Gate, 0.5f, 0.25f}})},
            {{"bandMode", 0.5f}}});
    add(t, {"Dotted Gate", "Gate Chops", {"gate", "dotted", "syncopated"},
            {scene("A Dotted 1/8", {{OT::Gate, 1.0f, 0.75f}}), scene("B Dotted 1/16", {{OT::Gate, 1.0f, 0.375f}}),
             scene("C Dotted 1/4", {{OT::Gate, 0.9f, 1.5f}}), scene("D Dotted Soft", {{OT::Gate, 0.5f, 0.75f}})}, {}});
    add(t, {"Gate Pump", "Gate Chops", {"gate", "pump", "sidechain"},
            {scene("A Pump 1/4", {{OT::Gate, 0.7f, 1.0f}}), scene("B Pump 1/8", {{OT::Gate, 0.7f, 0.5f}}),
             scene("C Deep Pump", {{OT::Gate, 1.0f, 1.0f}}), scene("D Slow Pump", {{OT::Gate, 0.6f, 2.0f}})},
            {{"bandMode", 0.5f}, {"bandCrossover", 0.4f}}});
    // ---- Tape Stops ----
    add(t, {"Bar End Tape Stop", "Tape Stops", {"tapestop", "fill", "bar"},
            {scene("A Last Bar Stop", {{OT::TapeStop, 1.0f, 1.0f}}, lastBar), scene("B Long Stop", {{OT::TapeStop, 1.0f, 2.0f}}, lastBar),
             scene("C Quick Stop", {{OT::TapeStop, 0.8f, 0.5f}}, lastBar), scene("D Always Stop", {{OT::TapeStop, 0.7f, 1.0f}})}, {}});
    add(t, {"Gentle Tape Slow", "Tape Stops", {"tapestop", "gentle", "wobble"},
            {scene("A Soft Slow", {{OT::TapeStop, 0.35f, 1.0f}}), scene("B Softer", {{OT::TapeStop, 0.25f, 2.0f}}),
             scene("C Medium", {{OT::TapeStop, 0.5f, 1.0f}}), scene("D Soft + Half", {{OT::TapeStop, 0.3f, 1.0f}, {OT::HalfTime, 0.3f, 1.0f}})},
            {{"dryWet", 0.75f}}});
    add(t, {"Phrase Tape Stop", "Tape Stops", {"tapestop", "phrase", "rare"},
            {scene("A Every 4th", {{OT::TapeStop, 1.0f, 1.0f}}, everyFourthPhrase),
             scene("B 4th + Last Bar", {{OT::TapeStop, 1.0f, 2.0f}}, lastBarEveryOther),
             scene("C Stop + Reverse", {{OT::TapeStop, 0.9f, 1.0f}, {OT::ReverseSlice, 0.6f, 1.0f}}, everyFourthPhrase),
             scene("D Slow Stop", {{OT::TapeStop, 0.4f, 2.0f}})}, {}});
    add(t, {"Vinyl Brake", "Tape Stops", {"tapestop", "brake", "dj"},
            {scene("A Brake", {{OT::TapeStop, 1.0f, 2.0f}}, lastBar), scene("B Brake + Repeat", {{OT::TapeStop, 1.0f, 1.0f}, {OT::Repeat, 0.6f, 0.5f}}, lastBar),
             scene("C Stutter Brake", {{OT::Repeat, 0.8f, 0.25f}, {OT::TapeStop, 0.8f, 1.0f}}, backbeatTapeEvents),
             scene("D Reverse Brake", {{OT::ReverseSlice, 0.8f, 1.0f}, {OT::TapeStop, 0.8f, 1.0f}}, lastBar)}, {}});
    // ---- Builds & Fills ----
    add(t, {"Snare Roll Build", "Builds & Fills", {"build", "roll", "fill"},
            {scene("A Quarter Roll", {{OT::Repeat, 1.0f, 1.0f}}, lastBar), scene("B Eighth Roll", {{OT::Repeat, 1.0f, 0.5f}}, lastBar),
             scene("C Sixteenth Roll", {{OT::Repeat, 1.0f, 0.25f}}, lastBar), scene("D Roll Peak", {{OT::Repeat, 1.0f, 0.125f}, {OT::Gate, 0.6f, 0.125f}}, lastBar)}, {}});
    add(t, {"Last Bar Fill", "Builds & Fills", {"fill", "bar", "drums"},
            {scene("A Repeat Fill", {{OT::Repeat, 1.0f, 0.25f}}, lastBar), scene("B Reverse Fill", {{OT::ReverseSlice, 1.0f, 0.5f}}, lastBar),
             scene("C Mixed Fill", {{OT::Repeat, 0.8f, 0.125f}, {OT::ReverseSlice, 0.7f, 0.5f}}, lastBar),
             scene("D Fill + Stop", {{OT::Repeat, 0.8f, 0.25f}, {OT::TapeStop, 0.8f, 1.0f}}, lastBarEveryOther)}, {}});
    add(t, {"Reverse Riser", "Builds & Fills", {"build", "reverse", "riser"},
            {scene("A Riser 1", {{OT::ReverseSlice, 1.0f, 2.0f}}, lastBar), scene("B Riser 1/2", {{OT::ReverseSlice, 1.0f, 1.0f}}, lastBar),
             scene("C Riser 1/4", {{OT::ReverseSlice, 1.0f, 0.5f}}, lastBar), scene("D Riser Gate", {{OT::ReverseSlice, 0.9f, 0.5f}, {OT::Gate, 0.5f, 0.25f}}, lastBar)}, {}});
    add(t, {"Pickup Roll", "Builds & Fills", {"build", "pickup", "events"},
            {scene("A Beat 3 Pickup", {{OT::Repeat, 1.0f, 0.125f}, {OT::Gate, 0.8f, 0.25f}}, pickupEvents),
             scene("B Roll Steps", {{OT::Repeat, 1.0f, 0.25f}}, snareRollEvents),
             scene("C Pickup Soft", {{OT::Repeat, 0.6f, 0.125f}, {OT::Gate, 0.5f, 0.25f}}, pickupEvents),
             scene("D Roll Steps 2", {{OT::Repeat, 0.8f, 0.25f}}, snareRollEvents)}, {}});
    // ---- Subtle Groove ----
    add(t, {"Light Swing Gate", "Subtle Groove", {"subtle", "gate", "swing"},
            {scene("A Light 1/8", {{OT::Gate, 0.3f, 0.5f}}), scene("B Light 1/16", {{OT::Gate, 0.25f, 0.25f}}),
             scene("C Light Dotted", {{OT::Gate, 0.3f, 0.75f}}), scene("D Lighter", {{OT::Gate, 0.2f, 0.5f}})},
            {{"dryWet", 0.6f}}});
    add(t, {"Ghost Repeats", "Subtle Groove", {"subtle", "repeat", "ghost"},
            {scene("A Ghost 1/8", {{OT::Repeat, 0.25f, 0.5f}}), scene("B Ghost 1/16", {{OT::Repeat, 0.25f, 0.25f}}, protectDownbeat),
             scene("C Ghost 1/4", {{OT::Repeat, 0.3f, 1.0f}}, protectDownbeat), scene("D Ghost Mix", {{OT::Repeat, 0.2f, 0.25f}, {OT::Gate, 0.2f, 0.25f}}, protectDownbeatHalf)},
            {{"dryWet", 0.5f}}});
    add(t, {"Breathing Halftime", "Subtle Groove", {"subtle", "halftime", "gentle"},
            {scene("A Breath 1", {{OT::HalfTime, 0.3f, 1.0f}}), scene("B Breath 2", {{OT::HalfTime, 0.3f, 2.0f}}),
             scene("C Breath + Gate", {{OT::HalfTime, 0.25f, 1.0f}, {OT::Gate, 0.2f, 0.5f}}), scene("D Breath 4", {{OT::HalfTime, 0.35f, 4.0f}})},
            {{"dryWet", 0.65f}}});
    add(t, {"Offbeat Nudge", "Subtle Groove", {"subtle", "offbeat", "tape"},
            {scene("A Nudge", {{OT::TapeStop, 0.2f, 0.5f}, {OT::Gate, 0.2f, 0.5f}}, protectDownbeat),
             scene("B Nudge Repeat", {{OT::Repeat, 0.3f, 0.25f}}, protectDownbeatHalf),
             scene("C Nudge Rev", {{OT::ReverseSlice, 0.25f, 0.5f}}, protectDownbeat),
             scene("D Nudge All", {{OT::Repeat, 0.2f, 0.25f}, {OT::Gate, 0.2f, 0.5f}, {OT::TapeStop, 0.15f, 1.0f}}, limitTwo)},
            {{"dryWet", 0.55f}}});
    // ---- Experimental ----
    add(t, {"Everything Low", "Experimental", {"experimental", "all", "texture"},
            {scene("A All Low", {{OT::Repeat, 0.3f, 0.25f}, {OT::ReverseSlice, 0.3f, 0.5f}, {OT::Gate, 0.3f, 0.25f}, {OT::HalfTime, 0.3f, 1.0f}, {OT::TapeStop, 0.3f, 1.0f}}),
             scene("B All Mid", {{OT::Repeat, 0.5f, 0.125f}, {OT::ReverseSlice, 0.5f, 1.0f}, {OT::Gate, 0.5f, 0.5f}, {OT::HalfTime, 0.5f, 2.0f}, {OT::TapeStop, 0.5f, 1.0f}}, limitTwo),
             scene("C All Tight", {{OT::Repeat, 0.6f, 0.0625f}, {OT::ReverseSlice, 0.6f, 0.25f}, {OT::Gate, 0.6f, 0.0625f}}),
             scene("D All Wide", {{OT::HalfTime, 0.6f, 4.0f}, {OT::TapeStop, 0.6f, 2.0f}, {OT::ReverseSlice, 0.6f, 2.0f}})}, {}});
    add(t, {"Polyrhythm Gate", "Experimental", {"experimental", "polyrhythm", "gate"},
            {scene("A 3 vs 5", {{OT::Gate, 1.0f, 0.25f}, {OT::Repeat, 0.7f, 0.25f}}, polyGate),
             scene("B Gate 3", {{OT::Gate, 0.9f, 0.5f}}, polyGate), scene("C Repeat 5", {{OT::Repeat, 0.8f, 0.5f}}, polyGate),
             scene("D Free Gate", {{OT::Gate, 0.8f, 0.375f}})}, {}});
    add(t, {"Protected Downbeat", "Experimental", {"experimental", "downbeat", "groove"},
            {scene("A Repeat Offbeats", {{OT::Repeat, 1.0f, 0.25f}}, protectDownbeat), scene("B Gate Offbeats", {{OT::Gate, 1.0f, 0.25f}}, protectDownbeatHalf),
             scene("C Reverse Offbeats", {{OT::ReverseSlice, 0.9f, 0.5f}}, protectDownbeat),
             scene("D Locked Repeat", {{OT::Repeat, 0.8f, 0.25f}, {OT::TapeStop, 0.6f, 1.0f}}, lockRepeatDepth)}, {}});
    add(t, {"Wide Spectrum Morph", "Experimental", {"experimental", "morph", "scenes"},
            {scene("A Source", {{OT::Repeat, 0.2f, 0.5f}}), scene("B Target", {{OT::Repeat, 1.0f, 0.0625f}, {OT::ReverseSlice, 1.0f, 0.25f}}),
             scene("C Heavy", {{OT::HalfTime, 1.0f, 2.0f}, {OT::TapeStop, 1.0f, 2.0f}}), scene("D Glitch", {{OT::Gate, 1.0f, 0.0625f}, {OT::Repeat, 0.8f, 0.125f}})},
            {{"morphTarget", 0.5f}, {"morphAmount", 0.5f}}});
    return t;
}
} // namespace

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> table = build();
    return table;
}
} // namespace groovemorph::core::preset
