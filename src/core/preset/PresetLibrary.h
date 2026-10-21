#pragma once

#include "core/preset/Preset.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace groovemorph::core::preset
{
inline constexpr std::size_t maxUserPresets = 512;

enum class LibraryStatus
{
    Ok,
    InvalidName,    // empty after sanitising
    FactoryName,    // collides with a factory preset (factory presets are immutable)
    Exists,         // a user preset of that name exists and overwrite was not requested
    NotFound,
    LimitReached,
    InvalidPreset,  // scene blob unreadable
};

struct Filter
{
    std::string search; // case-insensitive substring over name, category, tags, author
    std::string tag;    // exact (case-insensitive) tag, empty = any
    std::string category; // exact (case-insensitive), empty = any
};

// Message-model of the combined list: factory presets first (table order), then user presets
// sorted by name (case-insensitive). Not thread safe; the owner serialises access.
class PresetLibrary final
{
public:
    PresetLibrary();

    [[nodiscard]] std::size_t size() const noexcept { return entries.size(); }
    [[nodiscard]] std::size_t factoryCount() const noexcept { return factorySize; }
    [[nodiscard]] const Preset& at(std::size_t index) const noexcept { return entries[index]; }
    [[nodiscard]] int indexOf(std::string_view name) const noexcept; // case-insensitive, -1 if absent

    // Replace all user presets (invalid, factory-named and duplicate entries are skipped).
    void setUserPresets(std::vector<Preset> presets);
    // Add or (when overwrite) replace a user preset.
    [[nodiscard]] LibraryStatus addUser(Preset preset, bool overwrite);
    [[nodiscard]] LibraryStatus renameUser(std::string_view from, std::string_view to);
    [[nodiscard]] LibraryStatus removeUser(std::string_view name);

    // Indices into the combined list, in list order.
    [[nodiscard]] std::vector<std::size_t> filter(const Filter& f) const;
    [[nodiscard]] std::vector<std::string> categories() const; // factory order first, then user-only ones
    [[nodiscard]] std::vector<std::string> allTags() const;    // sorted, unique

private:
    void sortUser();
    std::vector<Preset> entries;
    std::size_t factorySize = 0;
};
} // namespace groovemorph::core::preset
