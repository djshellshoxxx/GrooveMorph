#include "core/MidiLinks.h"
#include "core/OperationParams.h"

#include <algorithm>
#include <charconv>

namespace groovemorph::core
{
bool MidiLinkTable::validTarget(std::string_view t) noexcept
{
    if (t.size() < 3 || t.size() > 64)
        return false;
    for (const char c : t)
        if (c < 0x21 || c > 0x7e || c == ';' || c == '=')
            return false;
    if (t.starts_with("p:"))
        return t.size() > 2;
    if (t.starts_with("s:"))
    {
        // s:<0..6>:<depth|slice|enabled>
        if (t.size() < 5 || t[2] < '0' || t[2] >= static_cast<char>('0' + operationCount) || t[3] != ':')
            return false;
        const auto field = t.substr(4);
        return field == "depth" || field == "slice" || field == "enabled";
    }
    return false;
}

MidiLinkTable::LinkResult MidiLinkTable::linkChecked(int cc, std::string_view target)
{
    if (cc < 0 || cc > 127 || !validTarget(target))
        return LinkResult::Invalid;
    for (auto& e : entries)
        if (e.target == target)
        {
            e.cc = cc;
            return LinkResult::Ok;
        }
    if (entries.size() >= maxLinks)
        return LinkResult::TableFull;
    entries.push_back({cc, std::string(target)});
    return LinkResult::Ok;
}

bool MidiLinkTable::link(int cc, std::string_view target)
{
    return linkChecked(cc, target) == LinkResult::Ok;
}

void MidiLinkTable::unlink(std::string_view target)
{
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const MidiLink& e) { return e.target == target; }),
                  entries.end());
}

int MidiLinkTable::ccFor(std::string_view target) const noexcept
{
    for (const auto& e : entries)
        if (e.target == target)
            return e.cc;
    return -1;
}

bool MidiLinkTable::usesCc(int cc) const noexcept
{
    return std::any_of(entries.begin(), entries.end(), [cc](const MidiLink& link) { return link.cc == cc; });
}

std::string MidiLinkTable::sharedTargets(int cc, std::string_view excluding) const
{
    std::string result;
    for (const auto& link : entries)
        if (link.cc == cc && link.target != excluding)
        {
            if (!result.empty()) result += ", ";
            result += link.target;
        }
    return result;
}

std::string MidiLinkTable::serialize() const
{
    std::string s;
    for (const auto& e : entries)
    {
        if (!s.empty())
            s += ';';
        s += std::to_string(e.cc) + '=' + e.target;
    }
    return s;
}

MidiLinkTable MidiLinkTable::deserialize(std::string_view text)
{
    MidiLinkTable t;
    std::size_t pos = 0;
    while (pos <= text.size())
    {
        auto end = text.find(';', pos);
        if (end == std::string_view::npos)
            end = text.size();
        const auto item = text.substr(pos, end - pos);
        pos = end + 1;
        const auto eq = item.find('=');
        if (eq == std::string_view::npos || eq == 0)
            continue;
        int cc = -1;
        const auto number = item.substr(0, eq);
        const auto [ptr, ec] = std::from_chars(number.data(), number.data() + number.size(), cc);
        if (ec != std::errc{} || ptr != number.data() + number.size())
            continue;
        t.link(cc, item.substr(eq + 1));
        if (end >= text.size())
            break;
    }
    return t;
}

CcInbox::CcInbox() noexcept
{
    for (auto& v : latest)
        v.store(-1, std::memory_order_relaxed);
    for (auto& v : travelTotal)
        v.store(0, std::memory_order_relaxed);
    seen.fill(-1);
    seenTravel.fill(0);
}

void CcInbox::push(int cc, int value) noexcept
{
    if (cc < 0 || cc >= 128)
        return;
    const auto i = static_cast<std::size_t>(cc);
    const int v = value & 0x7f;
    const int previous = latest[i].load(std::memory_order_relaxed);
    // The first event of a CC has no previous value; count it as one step so it registers as movement.
    const int step = previous < 0 ? 1 : (v > previous ? v - previous : previous - v);
    if (step != 0)
        travelTotal[i].fetch_add(static_cast<std::uint32_t>(step), std::memory_order_relaxed);
    latest[i].store(static_cast<std::int16_t>(v), std::memory_order_relaxed);
}

std::size_t CcInbox::collect(Change* out, std::size_t capacity) noexcept
{
    std::size_t n = 0;
    for (std::size_t cc = 0; cc < 128 && n < capacity; ++cc)
    {
        // Travel first, then the value: a push that lands in between is reported by the next collect().
        const auto travel = travelTotal[cc].load(std::memory_order_relaxed);
        const auto v = latest[cc].load(std::memory_order_relaxed);
        if (v >= 0 && (v != seen[cc] || travel != seenTravel[cc]))
        {
            const auto moved = travel - seenTravel[cc]; // unsigned wrap-around is intended
            seen[cc] = v;
            seenTravel[cc] = travel;
            if (out != nullptr)
                out[n] = {static_cast<int>(cc), static_cast<int>(v),
                          static_cast<int>(std::min<std::uint32_t>(moved, 0x7fffffffu))};
            ++n;
        }
    }
    return n;
}

void CcInbox::discardPending() noexcept
{
    for (std::size_t cc = 0; cc < 128; ++cc)
    {
        seenTravel[cc] = travelTotal[cc].load(std::memory_order_relaxed);
        seen[cc] = latest[cc].load(std::memory_order_relaxed);
    }
}

int pickLearnCc(const CcInbox::Change* changes, std::size_t count, bool macrosEnabled) noexcept
{
    int best = -1;
    int bestTravel = 0;
    for (std::size_t i = 0; changes != nullptr && i < count; ++i)
        if (!(macrosEnabled && isMacroCc(changes[i].cc))
            && (changes[i].travel > bestTravel || (changes[i].travel == bestTravel && bestTravel > 0 && changes[i].cc < best)))
        {
            best = changes[i].cc;
            bestTravel = changes[i].travel;
        }
    return best;
}
} // namespace groovemorph::core
