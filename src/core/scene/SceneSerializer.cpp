#include "core/scene/SceneSerializer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <exception>
#include <system_error>
#include <type_traits>

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
    // Phrase placement / event lane / permissions / activity keys. All optional: files written
    // before these keys existed (and by older builds) keep default, inert control state.
    void applyControl(std::size_t idx, std::string_view fullKey, std::string_view rest,
                      std::string_view value)
    {
        auto settings = result.bank.variation(idx).settings;
        auto& c = settings.control;
        long long n = 0;
        float f = 0.0f;
        const auto bad = [&] { note("invalid " + std::string(fullKey) + "; default kept"); };
        const auto asInt = [&](auto& out) {
            if (!parseNumber(value, n)) { bad(); return false; }
            out = static_cast<std::remove_reference_t<decltype(out)>>(n);
            return true;
        };
        const auto asFloat = [&](float& out) {
            if (!parseNumber(value, f)) { bad(); return false; }
            out = f;
            return true;
        };
        const auto asBool = [&](bool& out) {
            if (value == "1") out = true;
            else if (value == "0") out = false;
            else { bad(); return false; }
            return true;
        };
        bool known = true;
        if (rest == "placement.enabled") asBool(c.placement.enabled);
        else if (rest == "placement.window")
        {
            if (parseNumber(value, n) && n >= 0 && n <= 2) c.placement.window = static_cast<control::WindowMode>(n);
            else bad();
        }
        else if (rest == "placement.start") asFloat(c.placement.startPhase);
        else if (rest == "placement.span") asFloat(c.placement.spanPhase);
        else if (rest == "placement.every") { if (asInt(n)) c.placement.every = static_cast<std::uint8_t>(std::clamp<long long>(n, 1, 64)); }
        else if (rest == "placement.offset") { if (asInt(n)) c.placement.cadenceOffset = static_cast<std::uint8_t>(std::clamp<long long>(n, 0, 63)); }
        else if (rest == "placement.origin") { if (asInt(n)) c.placement.originTicks = static_cast<std::int32_t>(std::clamp<long long>(n, -1966080, 1966080)); }
        else if (rest == "placement.subdiv") { if (asInt(n)) c.placement.subdivision = static_cast<std::uint8_t>(std::clamp<long long>(n, 0, 255)); }
        else if (rest == "placement.protect") asFloat(c.placement.downbeatProtectBeats);
        else if (rest == "lane.explicit") asBool(c.lane.explicitEvents);
        else if (rest.starts_with("lane.loop."))
        {
            std::size_t op = 0;
            if (!parseIndex(rest.substr(10, 1), op) || op >= operationCount || rest.size() != 11)
                return;
            if (asInt(n)) c.lane.loopBeats[op] = static_cast<std::uint8_t>(std::clamp<long long>(n, 0, 64));
        }
        else if (rest == "activity.spacing") asFloat(c.activity.minSpacingBeats);
        else if (rest == "activity.coverage") asFloat(c.activity.maxCoverage);
        else if (rest.starts_with("event."))
        {
            const auto tail = rest.substr(6);
            const auto dot = tail.find('.');
            long long k = -1;
            if (dot == std::string_view::npos || !parseNumber(tail.substr(0, dot), k) || k < 0
                || k >= static_cast<long long>(control::maxLaneEvents))
                return;
            const auto field = tail.substr(dot + 1);
            auto& e = c.lane.events[static_cast<std::size_t>(k)];
            if (c.lane.count <= k)
                c.lane.count = static_cast<std::uint8_t>(k + 1);
            if (field == "op") { if (asInt(n)) e.operation = static_cast<std::uint8_t>(std::clamp<long long>(n, 0, 255)); }
            else if (field == "enabled") asBool(e.enabled);
            else if (field == "onset") { if (asInt(n)) e.onsetTicks = static_cast<std::int32_t>(std::clamp<long long>(n, 0, 1966080)); }
            else if (field == "duration") { if (asInt(n)) e.durationTicks = static_cast<std::int32_t>(std::clamp<long long>(n, 1, 1966080)); }
            else if (field == "depth") asFloat(e.depth);
            else if (field == "slice") asFloat(e.sliceBeats);
            else if (field == "priority") { if (asInt(n)) e.priority = static_cast<std::uint8_t>(std::clamp<long long>(n, 0, 100)); }
            else if (field == "restart") asBool(e.restartCycle);
            else known = false;
        }
        else if (rest.starts_with("perm.op."))
        {
            std::size_t op = 0;
            if (!parseIndex(rest.substr(8, 1), op) || op >= operationCount || rest.size() != 9)
                return;
            if (asInt(n)) c.permissions.operationLocks[op] = static_cast<std::uint8_t>(n & control::lock::all);
        }
        else if (rest == "perm.placement") asBool(c.permissions.placementLocked);
        else if (rest == "activity.enabled") asBool(c.activity.enabled);
        else if (rest == "activity.max") { if (asInt(n)) c.activity.maxConcurrent = static_cast<std::uint8_t>(std::clamp<long long>(n, 1, 5)); }
        else if (rest.starts_with("activity.prio."))
        {
            std::size_t op = 0;
            if (!parseIndex(rest.substr(14, 1), op) || op >= operationCount || rest.size() != 15)
                return;
            if (asInt(n)) c.activity.priority[op] = static_cast<std::uint8_t>(std::clamp<long long>(n, 0, 100));
        }
        else
            known = false;
        if (!known)
            return; // unknown key: ignored for forward compatibility
        if (control::sanitize(c))
            note(std::string(fullKey) + " out of range; clamped");
        result.bank.setVariationSettings(idx, settings);
    }

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
        {
            applyControl(idx, fullKey, rest, value);
            return;
        }
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
    // Scene control keys are written only when non-default so legacy-equivalent scenes serialize
    // byte-identically to earlier builds.
    for (std::size_t i = 0; i < variationCount; ++i)
    {
        const auto& c = bank.variation(i).settings.control;
        if (control::isDefault(c))
            continue;
        const std::string prefix = "variation." + std::to_string(i) + ".";
        const auto& p = c.placement;
        line(prefix + "placement.enabled", p.enabled ? "1" : "0");
        line(prefix + "placement.window", std::to_string(static_cast<int>(p.window)));
        line(prefix + "placement.start", formatFloat(p.startPhase));
        line(prefix + "placement.span", formatFloat(p.spanPhase));
        line(prefix + "placement.every", std::to_string(p.every));
        line(prefix + "placement.offset", std::to_string(p.cadenceOffset));
        line(prefix + "placement.origin", std::to_string(p.originTicks));
        line(prefix + "placement.subdiv", std::to_string(p.subdivision));
        line(prefix + "placement.protect", formatFloat(p.downbeatProtectBeats));
        line(prefix + "lane.explicit", c.lane.explicitEvents ? "1" : "0");
        for (std::size_t k = 0; k < c.lane.count; ++k)
        {
            const auto& e = c.lane.events[k];
            const std::string ep = prefix + "event." + std::to_string(k) + ".";
            line(ep + "op", std::to_string(e.operation));
            line(ep + "enabled", e.enabled ? "1" : "0");
            line(ep + "onset", std::to_string(e.onsetTicks));
            line(ep + "duration", std::to_string(e.durationTicks));
            line(ep + "depth", formatFloat(e.depth));
            line(ep + "slice", formatFloat(e.sliceBeats));
            line(ep + "priority", std::to_string(e.priority));
            line(ep + "restart", e.restartCycle ? "1" : "0");
        }
        for (std::size_t op = 0; op < operationCount; ++op)
        {
            line(prefix + "perm.op." + std::to_string(op), std::to_string(c.permissions.operationLocks[op]));
            line(prefix + "activity.prio." + std::to_string(op), std::to_string(c.activity.priority[op]));
        }
        for (std::size_t op = 0; op < operationCount; ++op)
            line(prefix + "lane.loop." + std::to_string(op), std::to_string(c.lane.loopBeats[op]));
        line(prefix + "activity.spacing", formatFloat(c.activity.minSpacingBeats));
        line(prefix + "activity.coverage", formatFloat(c.activity.maxCoverage));
        line(prefix + "perm.placement", c.permissions.placementLocked ? "1" : "0");
        line(prefix + "activity.enabled", c.activity.enabled ? "1" : "0");
        line(prefix + "activity.max", std::to_string(c.activity.maxConcurrent));
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
