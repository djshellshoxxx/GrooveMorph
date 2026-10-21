#include "core/scene/SceneSerializer.h"

#include <cstdint>
#include <string>
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
    // Round trip of defaults and an edited bank.
    SceneBank bank;
    auto r = SceneSerializer::deserialize(SceneSerializer::serialize(bank));
    expect("default round trip", r.bank == bank && r.report.status == LoadStatus::Ok
                                     && r.report.messages.empty());

    bank.setSeed(0xFFFFFFFFFFFFFFFFull);
    bank.setActiveIndex(2);
    bank.setVariationName(1, "  odd = name #1 ");
    auto s = bank.variation(3).settings;
    s[OperationType::Gate] = OperationSettings{true, 0.1f + 0.2f, 1.0f / 3.0f};
    s[OperationType::TapeStop].depth = 0.123456789f;
    bank.setVariationSettings(3, s);
    MidiNoteMap map;
    map.notes = {0, 127, 64, 64};
    map.noteOffPolicy = NoteOffPolicy::ReturnToPrevious;
    bank.setMidiNoteMap(map);
    const std::string text = SceneSerializer::serialize(bank);
    expect("header", text.starts_with("groovemorph-scenes v1\n"));
    r = SceneSerializer::deserialize(text);
    expect("edited round trip", r.bank == bank && r.report.status == LoadStatus::Ok);
    expect("re-serialize identical", SceneSerializer::serialize(r.bank) == text);

    // CRLF and unknown keys / comments.
    std::string crlf;
    for (char c : text)
    {
        if (c == '\n')
            crlf += '\r';
        crlf += c;
    }
    r = SceneSerializer::deserialize(crlf + "future.key=42\r\n# comment\r\n");
    expect("crlf + unknown keys", r.bank == bank && r.report.status == LoadStatus::Ok);

    // Unknown version / missing header.
    r = SceneSerializer::deserialize("groovemorph-scenes v2\nseed=5\n");
    expect("unknown version defaults", r.bank == SceneBank{} && r.report.status == LoadStatus::Defaulted
                                           && !r.report.messages.empty());
    r = SceneSerializer::deserialize("");
    expect("empty defaults", r.bank == SceneBank{} && r.report.status == LoadStatus::Defaulted);

    // Out-of-range and malformed values.
    r = SceneSerializer::deserialize("groovemorph-scenes v1\nactive=9\nseed=abc\n"
                                     "variation.0.op.repeat.depth=7\n"
                                     "variation.0.op.repeat.slice=nan\n"
                                     "variation.1.op.gate.enabled=yes\n"
                                     "midi.note.0=300\nmidi.noteoff=sometimes\nnoequals\n"
                                     "variation.9.name=x\n");
    expect("recovered status", r.report.status == LoadStatus::Recovered
                                   && r.report.messages.size() >= 7);
    expect("active clamped", r.bank.activeIndex() == 3);
    expect("seed default", r.bank.seed() == defaultSeed);
    expect("depth clamped", r.bank.variation(0).settings[OperationType::Repeat].depth == 1.0f);
    expect("slice default", r.bank.variation(0).settings[OperationType::Repeat].sliceBeats == 0.25f);
    expect("enabled kept", r.bank.variation(1).settings[OperationType::Gate].enabled);
    expect("note clamped", r.bank.midiNoteMap().notes[0] == 127);
    expect("policy default", r.bank.midiNoteMap().noteOffPolicy == NoteOffPolicy::Ignore);

    // Fuzz-ish: every truncation, plus deterministic byte corruption, never crashes and always
    // yields a valid (in-range) bank.
    auto valid = [](const SceneBank& b) {
        if (b.activeIndex() >= variationCount)
            return false;
        for (std::size_t i = 0; i < variationCount; ++i)
        {
            if (b.variation(i).name.size() > maxVariationNameLength)
                return false;
            for (const auto& op : b.variation(i).settings.operations)
                if (!(op.depth >= minDepth && op.depth <= maxDepth && op.sliceBeats >= minSliceBeats
                      && op.sliceBeats <= maxSliceBeats))
                    return false;
        }
        for (int n : b.midiNoteMap().notes)
            if (n < midiNoteMin || n > midiNoteMax)
                return false;
        return true;
    };
    bool allValid = true;
    for (std::size_t len = 0; len <= text.size(); ++len)
        allValid = valid(SceneSerializer::deserialize(std::string_view(text).substr(0, len)).bank)
                   && allValid;
    expect("truncations valid", allValid);
    std::uint32_t rng = 0x12345678u;
    for (int iter = 0; iter < 2000; ++iter)
    {
        std::string mutated = text;
        for (int k = 0; k < 8; ++k)
        {
            rng = rng * 1664525u + 1013904223u;
            const auto pos = (rng >> 8) % mutated.size();
            mutated[pos] = static_cast<char>(rng >> 24);
        }
        allValid = valid(SceneSerializer::deserialize(mutated).bank) && allValid;
    }
    expect("garbage valid", allValid);

    // A hostile blob of bad lines cannot make the recovery report grow without bound.
    {
        std::string hostile = "groovemorph-scenes v1\n";
        for (int i = 0; i < 20000; ++i)
            hostile += "variation.0.op.repeat.depth=notanumber\n";
        const auto r = SceneSerializer::deserialize(hostile);
        expect("report is bounded", r.report.messages.size() <= 65);
        expect("report says more were suppressed", !r.report.messages.empty() && r.report.messages.back().find("suppressed") != std::string::npos);
        expect("still recovered, not defaulted", r.report.status == LoadStatus::Recovered && valid(r.bank));
    }
    // A scene blob from a NEWER format is not readable: defaults plus a message that says so (the processor keeps
    // the user's current scenes in that case).
    {
        const auto r = SceneSerializer::deserialize("groovemorph-scenes v2\nactive=1\n");
        expect("newer version is reported", r.report.status == LoadStatus::Defaulted && !r.report.messages.empty()
                                                && r.report.messages.front().find("newer") != std::string::npos);
        const auto none = SceneSerializer::deserialize("");
        expect("empty text defaults", none.report.status == LoadStatus::Defaulted);
    }
    // Two scenes on one MIDI note load exactly as saved (status Ok) but the report mentions the conflict.
    {
        SceneBank bank;
        auto map = bank.midiNoteMap();
        map.notes[1] = map.notes[0];
        bank.setMidiNoteMap(map);
        const auto r = SceneSerializer::deserialize(SceneSerializer::serialize(bank));
        expect("duplicate notes load unchanged", r.report.status == LoadStatus::Ok && r.bank.midiNoteMap().notes[1] == r.bank.midiNoteMap().notes[0]);
        bool mentioned = false;
        for (const auto& m : r.report.messages)
            mentioned = mentioned || m.find("more than one scene") != std::string::npos;
        expect("duplicate notes are mentioned", mentioned);
    }

    // N01: FilterSweep and BitCrush round trip, including the operation-indexed control keys.
    {
        SceneBank n;
        auto st = n.variation(2).settings;
        st[OperationType::FilterSweep] = OperationSettings{true, 0.75f, 2.0f};
        st[OperationType::BitCrush] = OperationSettings{true, 0.5f, 0.125f};
        st.control.permissions.operationLocks[5] = 3;
        st.control.activity.priority[6] = 80;
        st.control.lane.loopBeats[6] = 5;
        st.control.lane.explicitEvents = true;
        control::LaneEvent e;
        e.operation = static_cast<std::uint8_t>(OperationType::BitCrush);
        e.onsetTicks = 960;
        e.durationTicks = 480;
        st.control.lane.events[0] = e;
        st.control.lane.count = 1;
        n.setVariationSettings(2, st);
        const std::string t = SceneSerializer::serialize(n);
        expect("new ops are written when used", t.find("variation.2.op.filtersweep.depth=0.75") != std::string::npos
                                                     && t.find("variation.2.op.bitcrush.slice=0.125") != std::string::npos);
        const auto back = SceneSerializer::deserialize(t);
        expect("new ops round trip", back.bank == n && back.report.status == LoadStatus::Ok && back.report.messages.empty());
        const auto& bs = back.bank.variation(2).settings;
        expect("new op values kept", bs[OperationType::FilterSweep].enabled && bs[OperationType::FilterSweep].sliceBeats == 2.0f
                                         && bs[OperationType::BitCrush].depth == 0.5f);
        expect("new lane control kept", bs.control.permissions.operationLocks[5] == 3 && bs.control.activity.priority[6] == 80
                                            && bs.control.lane.loopBeats[6] == 5
                                            && bs.control.lane.events[0].operation == static_cast<std::uint8_t>(OperationType::BitCrush));
        expect("new ops re-serialize identically", SceneSerializer::serialize(back.bank) == t);
        // Scenes that do not use the new effects serialize without any new key (byte-identical to earlier builds).
        const std::string plain = SceneSerializer::serialize(SceneBank{});
        expect("unused new ops are not written", plain.find("filtersweep") == std::string::npos && plain.find("bitcrush") == std::string::npos);
    }

    // N01 legacy fixture: a file written before FilterSweep/BitCrush existed (five operations, five-lane control keys).
    {
        const std::string legacy =
            "groovemorph-scenes v1\n"
            "seed=42\n"
            "active=1\n"
            "variation.0.name=A Repeat\n"
            "variation.0.op.repeat.enabled=1\n"
            "variation.0.op.repeat.depth=1\n"
            "variation.0.op.repeat.slice=0.25\n"
            "variation.0.op.tapestop.enabled=0\n"
            "variation.0.op.tapestop.depth=1\n"
            "variation.0.op.tapestop.slice=0.25\n"
            "variation.1.name=B Gate\n"
            "variation.1.op.gate.enabled=1\n"
            "variation.1.op.gate.depth=0.800000012\n"
            "variation.1.op.gate.slice=0.125\n"
            "variation.1.op.tapestop.enabled=1\n"
            "variation.1.op.tapestop.depth=0.600000024\n"
            "variation.1.op.tapestop.slice=0.5\n"
            "variation.1.placement.enabled=1\n"
            "variation.1.lane.explicit=1\n"
            "variation.1.event.0.op=4\n"
            "variation.1.event.0.onset=960\n"
            "variation.1.event.0.duration=480\n"
            "variation.1.perm.op.4=2\n"
            "variation.1.activity.prio.4=70\n"
            "variation.1.lane.loop.4=3\n"
            "variation.1.activity.enabled=1\n"
            "variation.1.activity.max=5\n"
            "midi.note.0=60\n"
            "midi.note.1=61\n"
            "midi.note.2=62\n"
            "midi.note.3=63\n"
            "midi.noteoff=ignore\n";
        const auto r2 = SceneSerializer::deserialize(legacy);
        expect("legacy file loads cleanly", r2.report.status == LoadStatus::Ok && r2.report.messages.empty());
        const auto& v0 = r2.bank.variation(0).settings;
        const auto& v1 = r2.bank.variation(1).settings;
        expect("legacy file keeps its scene values", v0[OperationType::Repeat].enabled && v1[OperationType::Gate].depth == 0.800000012f
                                                         && v1[OperationType::TapeStop].sliceBeats == 0.5f);
        bool newOpsOff = true;
        for (std::size_t i = 0; i < variationCount; ++i)
            for (const auto op : {OperationType::FilterSweep, OperationType::BitCrush})
                newOpsOff = newOpsOff && !r2.bank.variation(i).settings[op].enabled
                            && r2.bank.variation(i).settings[op].depth == OperationSettings{}.depth;
        expect("legacy file loads with both new ops disabled", newOpsOff);
        expect("legacy control keys keep their lane", v1.control.permissions.operationLocks[4] == 2
                                                          && v1.control.activity.priority[4] == 70 && v1.control.lane.loopBeats[4] == 3
                                                          && v1.control.lane.events[0].operation == 4 && v1.control.activity.maxConcurrent == 5);
        expect("new lanes get the defaults", v1.control.permissions.operationLocks[5] == 0 && v1.control.permissions.operationLocks[6] == 0
                                                 && v1.control.activity.priority[5] == 50 && v1.control.activity.priority[6] == 50
                                                 && v1.control.lane.loopBeats[5] == 0);
        const std::string rewritten = SceneSerializer::serialize(r2.bank);
        expect("legacy scene re-saves without writing the new op keys", rewritten.find("filtersweep") == std::string::npos
                                                                         && rewritten.find("bitcrush") == std::string::npos);
        expect("legacy scene survives a second round trip", SceneSerializer::deserialize(rewritten).bank == r2.bank);
        // Out-of-range operation index in new key space still drops the event.
        const auto bad = SceneSerializer::deserialize("groovemorph-scenes v1\nvariation.0.lane.explicit=1\nvariation.0.event.0.op=7\nvariation.0.event.0.onset=0\nvariation.0.event.0.duration=10\n");
        expect("operation 7 does not exist", bad.bank.variation(0).settings.control.lane.count == 0);
    }

    if (failures == 0)
        std::cout << "SceneSerializerTests passed\n";
    return failures == 0 ? 0 : 1;
}
