#include "core/scene/SceneSerializer.h"

#include <array>
#include <charconv>
#include <cmath>
#include <exception>
#include <system_error>

namespace groovemorph::core::scene
{
namespace
{
constexpr std::array<std::string_view, operationCount> opKeys{
    "repeat", "reverse", "gate", "halftime", "tapestop"};

std::string formatFloat(float v)
{
    std::array<char, 64> buf{};
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), v); // shortest round-trip
    return std::string(buf.data(), res.ptr);
}

template <typename T>
bool parseNumber(std::string_view s, T& out) noexcept
{
    if (s.empty())
        return false;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return res.ec == std::errc{} && res.ptr == s.data() + s.size();
}

bool parseIndex(std::string_view s, std::size_t& out) noexcept
{
    return s.size() == 1 && s[0] >= '0' && s[0] < static_cast<char>('0' + variationCount)
           && (out = static_cast<std::size_t>(s[0] - '0'), true);
}

std::string_view trimCR(std::string_view line) noexcept
{
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    return line;
}

class Parser
{
public:
    LoadResult result;

    void note(std::string msg)
    {
        result.report.status = LoadStatus::Recovered;
        result.report.messages.push_back(std::move(msg));
    }

    void apply(std::string_view key, std::string_view value)
    {
        if (key == "seed")
        {
            std::uint64_t seed = 0;
            if (parseNumber(value, seed))
                result.bank.setSeed(seed);
            else
                note("invalid seed; default kept");
        }
        else if (key == "active")
        {
            long long idx = 0;
            if (!parseNumber(value, idx))
                note("invalid active index; default kept");
            else
            {
                if (idx < 0 || idx >= static_cast<long long>(variationCount))
                    note("active index out of range; clamped");
                result.bank.setActiveIndex(
                    static_cast<std::size_t>(idx < 0 ? 0 : idx > 1000 ? 1000 : idx));
            }
        }
        else if (key == "midi.noteoff")
        {
            auto map = result.bank.midiNoteMap();
            if (value == "ignore")
                map.noteOffPolicy = NoteOffPolicy::Ignore;
            else if (value == "return")
                map.noteOffPolicy = NoteOffPolicy::ReturnToPrevious;
            else
                note("invalid midi.noteoff; default kept");
            result.bank.setMidiNoteMap(map);
        }
        else if (key.starts_with("midi.note."))
        {
            std::size_t idx = 0;
            int n = 0;
            if (!parseIndex(key.substr(10), idx))
                return; // unknown key
            if (!parseNumber(value, n))
                return note("invalid " + std::string(key) + "; default kept");
            if (n < midiNoteMin || n > midiNoteMax)
                note(std::string(key) + " out of range; clamped");
            auto map = result.bank.midiNoteMap();
            map.notes[idx] = n;
            result.bank.setMidiNoteMap(map);
        }
        else if (key.starts_with("variation."))
            applyVariation(key, key.substr(10), value);
    }

private:
    void applyVariation(std::string_view fullKey, std::string_view rest, std::string_view value)
    {
        std::size_t idx = 0;
        if (rest.size() < 3 || !parseIndex(rest.substr(0, 1), idx) || rest[1] != '.')
            return;
        rest.remove_prefix(2);
        if (rest == "name")
        {
            const auto clean = sanitizeName(value);
            if (clean != value)
                note(std::string(fullKey) + " sanitized");
            result.bank.setVariationName(idx, clean);
            return;
        }
        if (!rest.starts_with("op."))
            return;
        rest.remove_prefix(3);
        const auto dot = rest.find('.');
        if (dot == std::string_view::npos)
            return;
        const auto opName = rest.substr(0, dot);
        const auto field = rest.substr(dot + 1);
        std::size_t op = 0;
        while (op < operationCount && opKeys[op] != opName)
            ++op;
        if (op == operationCount)
            return;

        auto settings = result.bank.variation(idx).settings;
        auto& s = settings.operations[op];
        if (field == "enabled")
        {
            if (value == "1")
                s.enabled = true;
            else if (value == "0")
                s.enabled = false;
            else
                note("invalid " + std::string(fullKey) + "; default kept");
        }
        else if (field == "depth" || field == "slice")
        {
            float f = 0.0f;
            if (!parseNumber(value, f))
                return note("invalid " + std::string(fullKey) + "; default kept");
            (field == "depth" ? s.depth : s.sliceBeats) = f;
            if (sanitize(s))
                note(std::string(fullKey) + " out of range; clamped");
        }
        else
            return;
        result.bank.setVariationSettings(idx, settings);
    }
};
} // namespace

std::string SceneSerializer::serialize(const SceneBank& bank)
{
    std::string out;
    out.reserve(1024);
    auto line = [&out](std::string_view k, std::string_view v) {
        out.append(k).append("=").append(v).append("\n");
    };
    out.append(sceneFormatMagic).append(" v").append(std::to_string(sceneFormatVersion)).append("\n");
    line("seed", std::to_string(bank.seed()));
    line("active", std::to_string(bank.activeIndex()));
    for (std::size_t i = 0; i < variationCount; ++i)
    {
        const auto& v = bank.variation(i);
        const std::string prefix = "variation." + std::to_string(i) + ".";
        line(prefix + "name", v.name);
        for (std::size_t op = 0; op < operationCount; ++op)
        {
            const auto& s = v.settings.operations[op];
            const std::string p = prefix + "op." + std::string(opKeys[op]) + ".";
            line(p + "enabled", s.enabled ? "1" : "0");
            line(p + "depth", formatFloat(s.depth));
            line(p + "slice", formatFloat(s.sliceBeats));
        }
    }
    const auto& map = bank.midiNoteMap();
    for (std::size_t i = 0; i < variationCount; ++i)
        line("midi.note." + std::to_string(i), std::to_string(map.notes[i]));
    line("midi.noteoff", map.noteOffPolicy == NoteOffPolicy::ReturnToPrevious ? "return" : "ignore");
    return out;
}

LoadResult SceneSerializer::deserialize(std::string_view text) noexcept
{
    try
    {
        Parser parser;
        const auto firstEnd = text.find('\n');
        const auto header = trimCR(text.substr(0, firstEnd));
        const std::string expected =
            std::string(sceneFormatMagic) + " v" + std::to_string(sceneFormatVersion);
        if (header != expected)
        {
            LoadResult r;
            r.report.status = LoadStatus::Defaulted;
            r.report.messages.push_back(header.starts_with(sceneFormatMagic)
                                            ? "unsupported scene format version; defaults loaded"
                                            : "missing scene header; defaults loaded");
            return r;
        }
        std::size_t pos = firstEnd == std::string_view::npos ? text.size() : firstEnd + 1;
        while (pos < text.size())
        {
            auto end = text.find('\n', pos);
            if (end == std::string_view::npos)
                end = text.size();
            const auto line = trimCR(text.substr(pos, end - pos));
            pos = end + 1;
            if (line.empty() || line.front() == '#')
                continue;
            const auto eq = line.find('=');
            if (eq == std::string_view::npos)
            {
                parser.note("malformed line ignored");
                continue;
            }
            parser.apply(line.substr(0, eq), line.substr(eq + 1));
        }
        return std::move(parser.result);
    }
    catch (const std::exception&)
    {
        // Only reachable on allocation failure; fall back to defaults without propagating.
        try
        {
            LoadResult r;
            r.report.status = LoadStatus::Defaulted;
            return r;
        }
        catch (...)
        {
            std::terminate();
        }
    }
}
} // namespace groovemorph::core::scene
