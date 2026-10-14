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

    if (failures == 0)
        std::cout << "SceneSerializerTests passed\n";
    return failures == 0 ? 0 : 1;
}
