// F08 generator history file codec (docs/specs/F08-VARIATION-HISTORY.md, acceptance F08-T04 .. F08-T07).
#include "core/scene/SceneBank.h"
#include "core/variation/CandidateBuilder.h"
#include "core/variation/GeneratorHistory.h"

#include <cmath>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::variation;

namespace
{
int failures = 0;
void expect(std::string_view name, bool ok)
{
    if (!ok)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

HistoryEntry makeEntry(std::uint64_t seed, std::uint32_t candidate, Style style, std::size_t scene)
{
    HistoryEntry e;
    for (auto& op : e.source.operations)
        op = {true, 0.5f, 0.25f};
    e.source.operations[candidate % operationCount].depth = 0.75f;
    e.settings.seed = seed;
    e.settings.candidate = candidate;
    e.settings.style = style;
    e.settings.distance = 0.5f;
    e.settings.protectedOperations = candidate % 4;
    e.settings.generateEventPatterns = (candidate % 2) == 0;
    e.settings.phraseTicks = 16 * control::ticksPerBeat;
    e.scene = scene;
    return e;
}
} // namespace

int main()
{
    // F08-T04: write 24 entries, reload, recall equals the original candidate.
    {
        std::vector<HistoryEntry> entries;
        for (std::uint32_t i = 0; i < 24; ++i)
            entries.push_back(makeEntry(100 + i, i, static_cast<Style>(i % 3), i % variationCount));
        const auto text = serializeHistory(entries);
        const auto back = parseHistory(text);
        expect("F08-T04 24 entries round trip", back.size() == 24);
        bool same = back.size() == 24;
        for (std::size_t i = 0; same && i < back.size(); ++i)
        {
            same = back[i].scene == entries[i].scene && back[i].settings.seed == entries[i].settings.seed
                   && back[i].settings.candidate == entries[i].settings.candidate
                   && back[i].settings.style == entries[i].settings.style
                   && back[i].settings.distance == entries[i].settings.distance
                   && back[i].settings.protectedOperations == entries[i].settings.protectedOperations
                   && back[i].settings.generateEventPatterns == entries[i].settings.generateEventPatterns
                   && back[i].settings.phraseTicks == entries[i].settings.phraseTicks
                   && scene::sameSettings(back[i].source, entries[i].source);
            if (same)
            {
                const auto a = buildCandidate(entries[i].source, entries[i].settings);
                const auto b = buildCandidate(back[i].source, back[i].settings);
                same = scene::sameSettings(a.settings, b.settings);
            }
        }
        expect("F08-T04 recall regenerates the same candidate", same);
        expect("F08-T04 re-serialising is stable", serializeHistory(back) == text);
    }
    // F08-T05: hostile files are rejected without a crash.
    {
        std::vector<HistoryEntry> one{makeEntry(1, 1, Style::Creative, 1)};
        const auto good = serializeHistory(one);
        expect("F08-T05 good file parses", parseHistory(good).size() == 1);
        expect("F08-T05 empty", parseHistory("").empty());
        expect("F08-T05 wrong header", parseHistory("groovemorph-generator-history 9\n" + good.substr(good.find('\n') + 1)).empty());
        expect("F08-T05 header only", parseHistory(std::string(historyHeader) + "\n").empty());
        expect("F08-T05 binary junk", parseHistory(std::string("\x01\x02\xff\xfe junk \0 more", 17)).empty());
        // Truncated at every length: never crashes, never returns more than the source had.
        bool truncOk = true;
        for (std::size_t len = 0; len < good.size(); ++len)
            truncOk = truncOk && parseHistory(std::string_view(good).substr(0, len)).size() <= 1;
        expect("F08-T05 truncation at every length is safe", truncOk);
        std::string nan = good;
        const auto d = nan.find(" 0.5 ");
        if (d != std::string::npos)
            nan.replace(d, 5, " nan ");
        expect("F08-T05 NaN distance is rejected", parseHistory(nan).empty());
        std::string inf = good;
        const auto d2 = inf.find(" 0.5 ");
        if (d2 != std::string::npos)
            inf.replace(d2, 5, " inf ");
        expect("F08-T05 infinite distance is rejected", parseHistory(inf).empty());
        std::string badScene = good;
        badScene.replace(badScene.find("entry 1 "), 8, "entry 9 ");
        expect("F08-T05 scene index out of range is rejected", parseHistory(badScene).empty());
        std::string badStyle = good;
        // entry <scene> <seed> <style> ...
        badStyle.replace(badStyle.find("entry 1 1 1"), 11, "entry 1 1 7");
        expect("F08-T05 style out of range is rejected", parseHistory(badStyle).empty());
        std::string huge(maxHistoryFileBytes + 1, 'x');
        expect("F08-T05 oversized text is ignored", parseHistory(huge).empty());
        std::string unterminated = good.substr(0, good.rfind("end-entry"));
        expect("F08-T05 missing end-entry stops the parse", parseHistory(unterminated).empty());
        std::string phrase0 = good;
        const auto p = phrase0.find(" 15360\n");
        if (p != std::string::npos)
            phrase0.replace(p, 7, " 0\n");
        expect("F08-T05 phrase length 0 is rejected", parseHistory(phrase0).empty());
    }
    // F08-T06: 25 entries keep the newest 24, order stable.
    {
        std::vector<HistoryEntry> entries;
        for (std::uint32_t i = 0; i < 25; ++i)
            entries.push_back(makeEntry(500 + i, i, Style::Conservative, 0));
        const auto back = parseHistory(serializeHistory(entries));
        expect("F08-T06 only 24 entries are kept", back.size() == maxHistoryEntries);
        expect("F08-T06 the newest (first) entries survive in order",
               back.front().settings.seed == 500 && back.back().settings.seed == 523);
        // A hand-edited file with 30 entries is also capped on load.
        std::vector<HistoryEntry> many;
        for (std::uint32_t i = 0; i < 30; ++i)
            many.push_back(makeEntry(900 + i, i, Style::Creative, 0));
        std::string text = std::string(historyHeader) + "\n";
        const auto body = serializeHistory(std::vector<HistoryEntry>(many.begin(), many.begin() + 24));
        text += body.substr(body.find('\n') + 1);
        const auto extra = serializeHistory(std::vector<HistoryEntry>(many.begin() + 24, many.end()));
        text += extra.substr(extra.find('\n') + 1);
        expect("F08-T06 a 30-entry file loads 24", parseHistory(text).size() == maxHistoryEntries);
    }
    // F08-T07: a legacy history version is ignored, not misparsed.
    {
        const auto good = serializeHistory({makeEntry(1, 1, Style::Creative, 1)});
        std::string legacy = good;
        legacy.replace(legacy.find(" 2\n"), 3, " 1\n");
        expect("F08-T07 legacy header version 1 is ignored", parseHistory(legacy).empty());
        std::string future = good;
        future.replace(future.find(" 2\n"), 3, " 3\n");
        expect("F08-T07 future header version is ignored", parseHistory(future).empty());
    }
    if (failures == 0)
        std::cout << "GeneratorHistoryTests passed\n";
    return failures == 0 ? 0 : 1;
}
