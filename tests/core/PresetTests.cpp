#include "core/preset/FactoryPresets.h"
#include "core/preset/Preset.h"
#include "core/preset/PresetLibrary.h"
#include "core/scene/SceneSerializer.h"
#include "core/scene/UndoStack.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

using namespace groovemorph::core;
using namespace groovemorph::core::preset;
using groovemorph::core::scene::LoadStatus;
using groovemorph::core::scene::SceneBank;
using groovemorph::core::scene::SceneSerializer;

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

struct Rng
{
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    std::uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<std::uint32_t>(s >> 16);
    }
};

Preset sample()
{
    SceneBank bank;
    auto s = bank.variation(1).settings;
    s[OperationType::Gate] = OperationSettings{true, 0.5f, 0.375f};
    bank.setVariationSettings(1, s);
    return makePreset("My Preset", "Stutter", {"Tight", "drums", "tight"}, bank, "Me",
                      {{"dryWet", 0.25f}, {"bogus", 1.0f}});
}

bool validBank(const SceneBank& bank, std::string& why)
{
    for (std::size_t i = 0; i < variationCount; ++i)
    {
        const auto& v = bank.variation(i);
        auto copy = v.settings;
        if (scene::sanitize(copy)) { why = "sanitize changed scene " + std::to_string(i); return false; }
        if (v.name.empty() || v.name.size() > scene::maxVariationNameLength) { why = "name"; return false; }
        bool any = false;
        for (const auto& op : v.settings.operations)
        {
            if (!std::isfinite(op.depth) || !std::isfinite(op.sliceBeats)) { why = "nan"; return false; }
            if (op.depth < 0.0f || op.depth > 1.0f) { why = "depth"; return false; }
            if (op.sliceBeats < scene::minSliceBeats || op.sliceBeats > scene::maxSliceBeats) { why = "slice"; return false; }
            any = any || op.enabled;
        }
        const auto& c = v.settings.control;
        if (c.lane.explicitEvents) any = any || c.lane.count > 0;
        if (!any) { why = "scene does nothing"; return false; }
        for (auto l : c.permissions.operationLocks)
            if ((l & ~control::lock::all) != 0) { why = "lock bits"; return false; }
        if (c.lane.count > control::maxLaneEvents) { why = "lane count"; return false; }
        if (c.activity.maxConcurrent < 1 || c.activity.maxConcurrent > control::laneCount) { why = "activity"; return false; }
        if (!c.lane.explicitEvents && c.lane.count != 0) { why = "stray events"; return false; }
        for (std::size_t a = 0; a < c.lane.count; ++a)
        {
            const auto& e = c.lane.events[a];
            if (e.onsetTicks < 0 || e.durationTicks < 1 || e.onsetTicks + e.durationTicks > 3 * control::ticksPerBeat)
            { why = "event range"; return false; }
            if (!std::isfinite(e.depth) || !std::isfinite(e.sliceBeats)) { why = "event nan"; return false; }
            for (std::size_t b = a + 1; b < c.lane.count; ++b)
                if (e.operation == c.lane.events[b].operation
                    && control::eventsOverlap(e, c.lane.events[b], 3 * control::ticksPerBeat))
                { why = "event overlap"; return false; }
        }
        if (c.placement.every < 1 || c.placement.every > control::maxEvery) { why = "every"; return false; }
    }
    return true;
}
} // namespace

int main()
{
    // N02-T01
    const auto& table = factoryPresets();
    expect("T01 count", table.size() >= 24);
    expect("T01 Init first and equals the default bank", table[0].name == "Init" && *bankOf(table[0]) == SceneBank{});
    std::set<std::string> lowerNames;
    std::set<std::string> cats;
    for (const auto& p : table)
    {
        lowerNames.insert(toLower(p.name));
        cats.insert(p.category);
        expect("T01 factory flag", p.factory);
        expect("T01 name length", !p.name.empty() && p.name.size() <= maxNameLength);
        expect("T01 tags", !p.tags.empty() && p.tags.size() <= maxTags);
    }
    expect("T01 unique names", lowerNames.size() == table.size());
    for (const char* c : factoryCategories)
        expect(std::string("T01 category ") + c, cats.count(c) == 1);
    expect("T01 no stray category", cats.size() == std::size(factoryCategories));

    // N02-T02, T03
    for (const auto& p : table)
    {
        const auto loaded = SceneSerializer::deserialize(p.sceneText);
        expect("T02 loads Ok " + p.name, loaded.report.status == LoadStatus::Ok && loaded.report.messages.empty());
        std::string why;
        expect("T03 valid " + p.name + " " + why, validBank(loaded.bank, why));
        if (!why.empty()) std::cerr << "  (" << p.name << ": " << why << ")\n";
        expect("T03 reserializes identically " + p.name, SceneSerializer::serialize(loaded.bank) == p.sceneText);
        for (const auto& param : p.params)
            expect("T03 param allowed " + p.name, isAllowedParam(param.id) && param.value >= 0.0f && param.value <= 1.0f);
        // Whole file round trip.
        const auto parsed = parsePreset(serializePreset(p));
        expect("T05 factory file round trip " + p.name, parsed.ok && parsed.preset.sceneText == p.sceneText
                                                           && parsed.preset.name == p.name && parsed.preset.params == p.params);
    }
    // Explicit-event presets really got their events (addEvent failures are silent in the builder).
    for (const char* name : {"Pickup Roll", "Polyrhythm Gate", "Vinyl Brake"})
    {
        const auto idx = PresetLibrary().indexOf(name);
        expect("T03 named preset exists", idx >= 0);
        const auto bank = bankOf(table[static_cast<std::size_t>(idx)]);
        std::size_t events = 0;
        for (std::size_t i = 0; i < variationCount; ++i)
            events += bank->variation(i).settings.control.lane.count;
        expect(std::string("T03 events present ") + name, events >= 2);
    }
    {
        const auto bank = bankOf(table[static_cast<std::size_t>(PresetLibrary().indexOf("Pickup Roll"))]);
        expect("T03 roll steps all placed", bank->variation(1).settings.control.lane.count == 3);
    }

    // N02-T04
    {
        // Build again via a fresh library path: serialize every entry and compare to the static table.
        PresetLibrary a, b;
        bool same = a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i)
            same = a.at(i) == b.at(i);
        expect("T04 deterministic", same);
    }

    // N02-T05
    {
        const Preset p = sample();
        expect("T05 sanitized tags", p.tags.size() == 2 && p.tags[0] == "tight");
        expect("T05 params whitelist", p.params.size() == 1 && p.params[0].id == "dryWet");
        const std::string text = serializePreset(p);
        expect("T05 header", text.starts_with("groovemorph-preset v1\n"));
        const auto r = parsePreset(text);
        expect("T05 ok", r.ok && r.notes.empty());
        expect("T05 equal", r.preset == p);
        expect("T05 stable", serializePreset(r.preset) == text);
        const auto bank = bankOf(r.preset);
        expect("T05 bank", bank.has_value() && bank->variation(1).settings[OperationType::Gate].sliceBeats == 0.375f);
    }

    // N02-T06
    {
        std::string text = serializePreset(sample());
        std::string crlf;
        for (char c : text) { if (c == '\n') crlf += '\r'; crlf += c; }
        const auto r = parsePreset(crlf);
        expect("T06 CRLF", r.ok && r.preset.name == "My Preset" && r.preset.sceneText == sample().sceneText);
        auto extra = text;
        extra.insert(extra.find("tags="), "future=thing\nname=Second Name\n");
        const auto e = parsePreset(extra);
        expect("T06 unknown + duplicate", e.ok && e.preset.name == "My Preset");
        const std::string longName(500, 'x');
        auto longText = text;
        longText.replace(longText.find("My Preset"), 9, longName + "\xC3\xA9");
        const auto l = parsePreset(longText);
        expect("T06 truncated", l.ok && l.preset.name.size() == maxNameLength);
        auto uni = text;
        uni.replace(uni.find("My Preset"), 9, "Caf\xC3\xA9");
        const auto u = parsePreset(uni);
        expect("T06 non-ASCII", u.ok && u.preset.name == "Caf__");
        expect("T06 BOM", parsePreset("\xEF\xBB\xBF" + text).ok);
    }

    // N02-T07
    {
        const std::string good = serializePreset(sample());
        expect("T07 empty", !parsePreset("").ok);
        expect("T07 junk", !parsePreset("hello world").ok);
        expect("T07 scene file is not a preset", !parsePreset(SceneSerializer::serialize(SceneBank{})).ok);
        auto newer = good;
        newer.replace(0, 21, "groovemorph-preset v2");
        expect("T07 newer version", !parsePreset(newer).ok);
        auto noScenes = good.substr(0, good.find("scenes\n"));
        expect("T07 no scenes marker", !parsePreset(noScenes).ok);
        auto noName = good;
        noName.replace(noName.find("name=My Preset"), 14, "name=   ");
        expect("T07 empty name", !parsePreset(noName).ok);
        auto badScenes = good.substr(0, good.find("scenes\n") + 7) + "this is not a scene file\n";
        const auto bs = parsePreset(badScenes);
        expect("T07 unreadable scenes", !bs.ok && !bs.notes.empty());
        expect("T07 oversize", !parsePreset(std::string(maxFileBytes + 1, 'a')).ok);
        expect("T07 rejected result is empty", !bs.ok && bs.preset.name.empty() && bs.preset.sceneText.empty());
    }

    // N02-T08 hostile / fuzz
    {
        const std::string good = serializePreset(table[0]);
        for (std::size_t len = 0; len < good.size(); ++len)
        {
            const auto r = parsePreset(std::string_view(good).substr(0, len));
            if (r.ok)
            {
                const auto bank = bankOf(r.preset);
                bool sane = bank.has_value();
                for (std::size_t i = 0; sane && i < variationCount; ++i)
                {
                    auto settings = bank->variation(i).settings;
                    sane = !scene::sanitize(settings);
                }
                expect("T08 truncated ok is sane", sane);
                expect("T08 truncated name", !r.preset.name.empty());
            }
        }
        Rng rng;
        for (int round = 0; round < 2000; ++round)
        {
            std::string mutated = good;
            const int flips = 1 + static_cast<int>(rng.next() % 8);
            for (int i = 0; i < flips; ++i)
                mutated[rng.next() % mutated.size()] = static_cast<char>(rng.next() & 0xFF);
            const auto r = parsePreset(mutated);
            if (r.ok)
            {
                expect("T08 flipped name", !r.preset.name.empty() && r.preset.name.size() <= maxNameLength);
                expect("T08 flipped tags", r.preset.tags.size() <= maxTags && r.preset.params.size() <= maxParams);
                for (const auto& p : r.preset.params)
                    expect("T08 flipped params", std::isfinite(p.value) && p.value >= 0.0f && p.value <= 1.0f && isAllowedParam(p.id));
                const auto bank = bankOf(r.preset);
                expect("T08 flipped bank", bank.has_value());
                if (bank)
                {
                    auto settings = bank->variation(0).settings;
                    expect("T08 flipped sanitized", !scene::sanitize(settings));
                }
            }
        }
        std::string nul = good;
        nul.insert(nul.find("scenes\n") + 10, std::string(5, '\0'));
        (void)parsePreset(nul);
        std::string huge = "groovemorph-preset v1\nname=" + std::string(100000, 'A') + "\nscenes\n";
        expect("T08 huge line", !parsePreset(huge).ok);
        std::string manyLines = "groovemorph-preset v1\nname=X\n";
        for (int i = 0; i < 10000; ++i) manyLines += "k" + std::to_string(i) + "=v\n";
        expect("T08 10k metadata lines", !parsePreset(manyLines).ok);
        std::string manyTags = "groovemorph-preset v1\nname=X\ntags=";
        for (int i = 0; i < 5000; ++i) manyTags += "t" + std::to_string(i) + ",";
        manyTags += "\nscenes\n" + SceneSerializer::serialize(SceneBank{});
        const auto mt = parsePreset(manyTags);
        expect("T08 tag flood", mt.ok && mt.preset.tags.size() <= maxTags);
        std::string noNewlines(50000, '=');
        expect("T08 no newlines", !parsePreset(noNewlines).ok);
        expect("T08 only header", !parsePreset("groovemorph-preset v1").ok);
    }

    // N02-T09
    {
        const std::string text = "groovemorph-preset v1\nname=P\nparam.dryWet=7\nparam.morphAmount=nan\nparam.bandMode=-3\n"
                                 "param.bandSlope=inf\nparam.evil=1\nparam.dryWet=0.1\nparam.masterIntensity=0.25\nscenes\n"
                                 + SceneSerializer::serialize(SceneBank{});
        const auto r = parsePreset(text);
        expect("T09 ok", r.ok);
        auto find = [&](const char* id) -> const PresetParam* {
            for (const auto& p : r.preset.params) if (p.id == id) return &p;
            return nullptr;
        };
        expect("T09 clamp high", find("dryWet") != nullptr && find("dryWet")->value == 1.0f);
        expect("T09 clamp low", find("bandMode") != nullptr && find("bandMode")->value == 0.0f);
        expect("T09 nan dropped", find("morphAmount") == nullptr && find("bandSlope") == nullptr);
        expect("T09 unknown dropped", find("evil") == nullptr);
        expect("T09 valid kept", find("masterIntensity") != nullptr && find("masterIntensity")->value == 0.25f);
        expect("T09 notes", !r.notes.empty());
        std::string flood = "groovemorph-preset v1\nname=P\n";
        for (const char* id : {"masterIntensity", "dryWet", "morphTarget", "morphAmount", "bandMode", "bandCrossover", "bandSlope"})
            flood += std::string("param.") + id + "=0.5\n";
        flood += "scenes\n" + SceneSerializer::serialize(SceneBank{});
        expect("T09 all seven", parsePreset(flood).preset.params.size() == 7);
    }

    // N02-T10
    {
        SceneBank current;
        current.setSeed(1234);
        current.setActiveIndex(3);
        scene::MidiNoteMap map;
        map.notes = {10, 20, 30, 40};
        current.setMidiNoteMap(map);
        const auto presetBank = *bankOf(table[3]);
        const auto next = applyToBank(current, presetBank);
        expect("T10 scenes replaced", sameScenes(next, presetBank));
        expect("T10 keeps active/seed/midi", next.activeIndex() == 3 && next.seed() == 1234
                                               && next.midiNoteMap().notes == map.notes);
    }

    // N02-T11
    {
        for (const char* nasty : {"../../etc/passwd", "a/b\\c", "..", "...", "", "   ", ".hidden.", "CON", "com1", "lpt9.txt",
                                  "nul", "caf\xC3\xA9 \xF0\x9F\x98\x80", "name:with*bad?chars|<>\"", "a..b"})
        {
            const auto stem = sanitizeFileStem(nasty);
            expect(std::string("T11 stem nonempty ") + nasty, !stem.empty());
            expect(std::string("T11 no separators ") + nasty,
                   stem.find('/') == std::string::npos && stem.find('\\') == std::string::npos
                       && stem.find("..") == std::string::npos && stem.find(':') == std::string::npos);
            expect(std::string("T11 no edge dot/space ") + nasty, stem.front() != '.' && stem.front() != ' '
                                                                   && stem.back() != '.' && stem.back() != ' ');
            const auto lower = toLower(stem);
            expect(std::string("T11 not reserved ") + nasty,
                   lower != "con" && lower != "nul" && lower != "com1" && lower != "prn" && lower != "aux");
        }
        expect("T11 length cap", sanitizeFileStem(std::string(500, 'a')).size() <= maxStemLength);
        expect("T11 plain name kept", sanitizeFileStem("Big Stutter (v2)") == "Big Stutter (v2)");
        expect("T11 reserved suffixed", sanitizeFileStem("CON") == "CON_" && sanitizeFileStem("lpt9.txt") == "lpt9.txt_");
    }

    // N02-T12
    {
        PresetLibrary lib;
        const auto factory = lib.size();
        expect("T12 factory count", lib.factoryCount() == factory && factory >= 24);
        Preset mine = sample();
        mine.name = "zeta";
        expect("T12 add", lib.addUser(mine, false) == LibraryStatus::Ok);
        mine.name = "Alpha";
        expect("T12 add2", lib.addUser(mine, false) == LibraryStatus::Ok);
        expect("T12 order", lib.at(factory).name == "Alpha" && lib.at(factory + 1).name == "zeta");
        mine.name = "ALPHA";
        expect("T12 duplicate refused", lib.addUser(mine, false) == LibraryStatus::Exists);
        mine.params.clear();
        expect("T12 overwrite", lib.addUser(mine, true) == LibraryStatus::Ok && lib.size() == factory + 2);
        expect("T12 overwrite applied", lib.at(factory).params.empty());
        Preset clash = sample();
        clash.name = "half time drop";
        expect("T12 factory name refused", lib.addUser(clash, true) == LibraryStatus::FactoryName);
        expect("T12 factory overwrite refused", lib.addUser(table[0], true) == LibraryStatus::FactoryName);
        expect("T12 factory delete refused", lib.removeUser(table[0].name) == LibraryStatus::FactoryName);
        expect("T12 factory rename refused", lib.renameUser(table[0].name, "mine") == LibraryStatus::FactoryName);
        expect("T12 rename onto factory refused", lib.renameUser("zeta", table[1].name) == LibraryStatus::FactoryName);
        expect("T12 rename onto user refused", lib.renameUser("zeta", "alpha") == LibraryStatus::Exists);
        expect("T12 rename", lib.renameUser("zeta", "Beta") == LibraryStatus::Ok && lib.indexOf("zeta") < 0
                                 && lib.at(factory + 1).name == "Beta");
        expect("T12 rename case only", lib.renameUser("Beta", "BETA") == LibraryStatus::Ok);
        expect("T12 rename missing", lib.renameUser("nope", "x") == LibraryStatus::NotFound);
        expect("T12 empty name", lib.addUser(Preset{}, false) == LibraryStatus::InvalidName);
        Preset broken = sample();
        broken.name = "broken";
        broken.sceneText = "garbage";
        expect("T12 invalid blob", lib.addUser(broken, false) == LibraryStatus::InvalidPreset);
        expect("T12 remove", lib.removeUser("beta") == LibraryStatus::Ok && lib.size() == factory + 1);
        expect("T12 remove missing", lib.removeUser("beta") == LibraryStatus::NotFound);
        Preset stolen = sample();
        stolen.name = "x";
        stolen.factory = true;
        expect("T12 factory flag cleared", lib.addUser(stolen, false) == LibraryStatus::Ok && !lib.at(lib.indexOf("x")).factory);
        // Cap.
        PresetLibrary many;
        std::vector<Preset> flood;
        for (std::size_t i = 0; i < maxUserPresets + 20; ++i)
        {
            Preset p = sample();
            p.name = "u" + std::to_string(i);
            flood.push_back(p);
        }
        flood.push_back(table[0]); // factory clash skipped
        many.setUserPresets(flood);
        expect("T12 cap", many.size() == many.factoryCount() + maxUserPresets);
        Preset extra = sample();
        extra.name = "one more";
        expect("T12 limit", many.addUser(extra, false) == LibraryStatus::LimitReached);
        PresetLibrary dup;
        dup.setUserPresets({sample(), sample(), Preset{}});
        expect("T12 setUser skips duplicates and invalid", dup.size() == dup.factoryCount() + 1);
    }

    // N02-T13
    {
        PresetLibrary lib;
        Filter f;
        expect("T13 no filter", lib.filter(f).size() == lib.size());
        f.category = "stutter";
        const auto stutter = lib.filter(f);
        expect("T13 category", stutter.size() == 5);
        for (auto i : stutter) expect("T13 category member", lib.at(i).category == "Stutter");
        f = {};
        f.search = "GATE";
        const auto gate = lib.filter(f);
        expect("T13 search name/tag", gate.size() >= 4);
        f = {};
        f.tag = "tapestop";
        const auto tape = lib.filter(f);
        expect("T13 tag", tape.size() >= 4);
        for (auto i : tape) expect("T13 tag member", equalsIgnoreCase(lib.at(i).category, "Tape Stops"));
        f.search = "zzzzzz";
        expect("T13 none", lib.filter(f).empty());
        expect("T13 categories", lib.categories().size() == std::size(factoryCategories));
        f = {};
        f.category = "Init";
        expect("T13 init category", lib.filter(f).size() == 1);
        const auto tags = lib.allTags();
        expect("T13 tags sorted unique", !tags.empty() && std::is_sorted(tags.begin(), tags.end()));
    }

    // N02-T14 legacy scene text (pre-control keys, hand written) inside a preset
    {
        const std::string legacy =
            "groovemorph-scenes v1\nseed=5\nactive=0\nvariation.0.name=Old\nvariation.0.op.gate.enabled=1\n"
            "variation.0.op.gate.depth=0.5\nvariation.0.op.gate.slice=0.25\n";
        const std::string file = "groovemorph-preset v1\nname=Legacy\nscenes\n" + legacy;
        const auto r = parsePreset(file);
        expect("T14 loads", r.ok);
        const auto bank = bankOf(r.preset);
        expect("T14 inert control", bank.has_value() && control::isDefault(bank->variation(0).settings.control)
                                        && bank->variation(0).settings[OperationType::Gate].enabled);
    }

    // N02-T15
    {
        scene::UndoStack stack{SceneBank{}};
        const SceneBank original = stack.current();
        const auto presetBank = *bankOf(table[5]);
        stack.commit(applyToBank(stack.current(), presetBank));
        expect("T15 applied", sameScenes(stack.current(), presetBank) && stack.undoDepth() == 1);
        stack.commit(applyToBank(stack.current(), presetBank));
        expect("T15 same preset again is a no-op", stack.undoDepth() == 1);
        expect("T15 undo", stack.undo() && stack.current() == original && !stack.canUndo());
    }

    if (failures == 0)
        std::cout << "PresetTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
