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

bool MidiLinkTable::link(int cc, std::string_view target)
{
    if (cc < 0 || cc > 127 || !validTarget(target))
        return false;
    for (auto& e : entries)
        if (e.target == target)
        {
            e.cc = cc;
            return true;
        }
    if (entries.size() >= maxLinks)
        return false;
    entries.push_back({cc, std::string(target)});
    return true;
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
    while (pos <= text.size() && t.entries.size() < maxLinks)
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
    seen.fill(-1);
}

void CcInbox::push(int cc, int value) noexcept
{
    if (cc >= 0 && cc < 128)
        latest[static_cast<std::size_t>(cc)].store(static_cast<std::int16_t>(value & 0x7f), std::memory_order_relaxed);
}

std::size_t CcInbox::collect(Change* out, std::size_t capacity) noexcept
{
    std::size_t n = 0;
    for (std::size_t cc = 0; cc < 128 && n < capacity; ++cc)
    {
        const auto v = latest[cc].load(std::memory_order_relaxed);
        if (v >= 0 && v != seen[cc])
        {
            seen[cc] = v;
            if (out != nullptr)
                out[n] = {static_cast<int>(cc), static_cast<int>(v)};
            ++n;
        }
    }
    return n;
}

void CcInbox::discardPending() noexcept
{
    for (std::size_t cc = 0; cc < 128; ++cc)
        seen[cc] = latest[cc].load(std::memory_order_relaxed);
}
} // namespace groovemorph::core
