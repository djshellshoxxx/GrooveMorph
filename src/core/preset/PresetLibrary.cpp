#include "core/preset/PresetLibrary.h"

#include "core/preset/FactoryPresets.h"

#include <algorithm>
#include <set>

namespace groovemorph::core::preset
{
namespace
{
bool contains(std::string_view haystack, std::string_view needleLower)
{
    return toLower(haystack).find(needleLower) != std::string::npos;
}
} // namespace

PresetLibrary::PresetLibrary()
{
    entries = factoryPresets();
    factorySize = entries.size();
}

int PresetLibrary::indexOf(std::string_view name) const noexcept
{
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (equalsIgnoreCase(entries[i].name, name))
            return static_cast<int>(i);
    return -1;
}

void PresetLibrary::sortUser()
{
    std::stable_sort(entries.begin() + static_cast<std::ptrdiff_t>(factorySize), entries.end(),
                     [](const Preset& a, const Preset& b) { return toLower(a.name) < toLower(b.name); });
}

void PresetLibrary::setUserPresets(std::vector<Preset> presets)
{
    entries.resize(factorySize);
    for (auto& p : presets)
    {
        if (entries.size() - factorySize >= maxUserPresets)
            break;
        p.factory = false;
        p.name = sanitizeText(p.name, maxNameLength);
        if (p.name.empty() || indexOf(p.name) >= 0 || !bankOf(p).has_value())
            continue;
        entries.push_back(std::move(p));
    }
    sortUser();
}

LibraryStatus PresetLibrary::addUser(Preset preset, bool overwrite)
{
    preset.factory = false;
    preset.name = sanitizeText(preset.name, maxNameLength);
    if (preset.name.empty())
        return LibraryStatus::InvalidName;
    if (!bankOf(preset).has_value())
        return LibraryStatus::InvalidPreset;
    const int existing = indexOf(preset.name);
    if (existing >= 0 && static_cast<std::size_t>(existing) < factorySize)
        return LibraryStatus::FactoryName;
    if (existing >= 0)
    {
        if (!overwrite)
            return LibraryStatus::Exists;
        entries[static_cast<std::size_t>(existing)] = std::move(preset);
        sortUser();
        return LibraryStatus::Ok;
    }
    if (entries.size() - factorySize >= maxUserPresets)
        return LibraryStatus::LimitReached;
    entries.push_back(std::move(preset));
    sortUser();
    return LibraryStatus::Ok;
}

LibraryStatus PresetLibrary::renameUser(std::string_view from, std::string_view to)
{
    const auto newName = sanitizeText(to, maxNameLength);
    if (newName.empty())
        return LibraryStatus::InvalidName;
    const int source = indexOf(from);
    if (source < 0)
        return LibraryStatus::NotFound;
    if (static_cast<std::size_t>(source) < factorySize)
        return LibraryStatus::FactoryName;
    const int clash = indexOf(newName);
    if (clash >= 0 && clash != source)
        return static_cast<std::size_t>(clash) < factorySize ? LibraryStatus::FactoryName : LibraryStatus::Exists;
    entries[static_cast<std::size_t>(source)].name = newName;
    sortUser();
    return LibraryStatus::Ok;
}

LibraryStatus PresetLibrary::removeUser(std::string_view name)
{
    const int index = indexOf(name);
    if (index < 0)
        return LibraryStatus::NotFound;
    if (static_cast<std::size_t>(index) < factorySize)
        return LibraryStatus::FactoryName;
    entries.erase(entries.begin() + index);
    return LibraryStatus::Ok;
}

std::vector<std::size_t> PresetLibrary::filter(const Filter& f) const
{
    const auto search = toLower(f.search);
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& p = entries[i];
        if (!f.category.empty() && !equalsIgnoreCase(p.category.empty() ? std::string_view("Uncategorized") : std::string_view(p.category), f.category))
            continue;
        if (!f.tag.empty()
            && std::none_of(p.tags.begin(), p.tags.end(), [&](const std::string& t) { return equalsIgnoreCase(t, f.tag); }))
            continue;
        if (!search.empty())
        {
            bool hit = contains(p.name, search) || contains(p.category, search) || contains(p.author, search);
            for (const auto& t : p.tags)
                hit = hit || contains(t, search);
            if (!hit)
                continue;
        }
        out.push_back(i);
    }
    return out;
}

std::vector<std::string> PresetLibrary::categories() const
{
    std::vector<std::string> out;
    for (const char* c : factoryCategories)
        out.emplace_back(c);
    for (const auto& p : entries)
    {
        const auto cat = p.category.empty() ? std::string("Uncategorized") : p.category;
        if (std::none_of(out.begin(), out.end(), [&](const std::string& c) { return equalsIgnoreCase(c, cat); }))
            out.push_back(cat);
    }
    return out;
}

std::vector<std::string> PresetLibrary::allTags() const
{
    std::set<std::string> tags;
    for (const auto& p : entries)
        tags.insert(p.tags.begin(), p.tags.end());
    return {tags.begin(), tags.end()};
}
} // namespace groovemorph::core::preset
