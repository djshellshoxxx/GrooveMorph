#include "PresetManager.h"

#include <algorithm>

namespace
{
namespace pre = groovemorph::core::preset;

juce::String describe(pre::LibraryStatus status)
{
    switch (status)
    {
        case pre::LibraryStatus::Ok: return {};
        case pre::LibraryStatus::InvalidName: return "Please give the preset a name.";
        case pre::LibraryStatus::FactoryName: return "That name belongs to a factory preset, which cannot be changed. Pick another name.";
        case pre::LibraryStatus::Exists: return "A preset with that name already exists.";
        case pre::LibraryStatus::NotFound: return "That preset no longer exists.";
        case pre::LibraryStatus::LimitReached: return "The user preset limit has been reached. Delete some presets first.";
        case pre::LibraryStatus::InvalidPreset: return "The preset data is not valid.";
    }
    return "Unknown error.";
}

PresetManager::Result fail(pre::LibraryStatus status, juce::String message = {})
{
    PresetManager::Result r;
    r.status = status;
    r.message = message.isNotEmpty() ? message : describe(status);
    if (r.message.isEmpty())
        r.message = "Failed.";
    return r;
}

std::string lowerKey(const std::string& name) { return pre::toLower(name); }

// Reads and parses one preset file; false (with a reason) on any problem. Never trusts the size on disk.
bool readPresetFile(const juce::File& file, pre::Preset& out, juce::String& reason)
{
    if (!file.existsAsFile() || file.isSymbolicLink())
    {
        reason = "The file could not be found.";
        return false;
    }
    if (file.getSize() > static_cast<juce::int64>(pre::maxFileBytes))
    {
        reason = "The file is too large to be a preset.";
        return false;
    }
    juce::MemoryBlock block;
    if (!file.loadFileAsData(block) || block.getSize() > pre::maxFileBytes)
    {
        reason = "The file could not be read.";
        return false;
    }
    const auto parsed = pre::parsePreset(std::string_view(static_cast<const char*>(block.getData()), block.getSize()));
    if (!parsed.ok)
    {
        reason = parsed.notes.empty() ? juce::String("Not a valid preset file.")
                                      : juce::String(parsed.notes.front());
        return false;
    }
    out = parsed.preset;
    return true;
}
} // namespace

juce::File PresetManager::defaultDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("djshellshoxxx")
        .getChildFile("GrooveMorph")
        .getChildFile("Presets");
}

PresetManager::PresetManager(juce::File userDirectory) : userDir(std::move(userDirectory))
{
    rescan();
}

void PresetManager::rescan()
{
    std::vector<pre::Preset> found;
    std::vector<juce::File> sources;
    if (userDir.isDirectory())
    {
        auto candidates = userDir.findChildFiles(juce::File::findFiles, false, "*" + juce::String(std::string(pre::fileExtension)));
        std::vector<juce::File> sorted(candidates.begin(), candidates.end());
        std::sort(sorted.begin(), sorted.end(), [](const juce::File& a, const juce::File& b) {
            return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
        });
        for (const auto& file : sorted)
        {
            pre::Preset preset;
            juce::String reason;
            if (readPresetFile(file, preset, reason))
            {
                found.push_back(std::move(preset));
                sources.push_back(file);
            }
        }
    }
    const std::lock_guard<std::mutex> lock(mutex);
    library.setUserPresets(found);
    files.clear();
    // Same first-wins rule as setUserPresets: a file is tracked only when its preset was accepted.
    for (std::size_t i = 0; i < found.size(); ++i)
    {
        const auto index = library.indexOf(found[i].name);
        if (index >= static_cast<int>(library.factoryCount()))
            files.emplace(lowerKey(library.at(static_cast<std::size_t>(index)).name), sources[i]);
    }
}

int PresetManager::count() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return static_cast<int>(library.size());
}

int PresetManager::factoryCount() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return static_cast<int>(library.factoryCount());
}

bool PresetManager::isFactory(int index) const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return index >= 0 && static_cast<std::size_t>(index) < library.factoryCount();
}

juce::String PresetManager::nameAt(int index) const
{
    const std::lock_guard<std::mutex> lock(mutex);
    if (index < 0 || static_cast<std::size_t>(index) >= library.size())
        return {};
    return juce::String(library.at(static_cast<std::size_t>(index)).name);
}

std::optional<pre::Preset> PresetManager::presetAt(int index) const
{
    const std::lock_guard<std::mutex> lock(mutex);
    if (index < 0 || static_cast<std::size_t>(index) >= library.size())
        return std::nullopt;
    return library.at(static_cast<std::size_t>(index));
}

int PresetManager::indexOf(const juce::String& name) const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return library.indexOf(name.toStdString());
}

std::vector<std::size_t> PresetManager::filter(const pre::Filter& f) const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return library.filter(f);
}

juce::StringArray PresetManager::categories() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    juce::StringArray out;
    for (const auto& c : library.categories())
        out.add(juce::String(c));
    return out;
}

juce::StringArray PresetManager::allTags() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    juce::StringArray out;
    for (const auto& t : library.allTags())
        out.add(juce::String(t));
    return out;
}

juce::File PresetManager::fileForNewName(const std::string& name) const
{
    // Caller holds the mutex. The stem never contains separators, so the file stays inside userDir.
    const auto stem = juce::String(pre::sanitizeFileStem(name));
    const auto ext = juce::String(std::string(pre::fileExtension));
    for (int n = 1; n < 1000; ++n)
    {
        const auto candidate = userDir.getChildFile(n == 1 ? stem + ext : stem + " (" + juce::String(n) + ")" + ext);
        const bool tracked = std::any_of(files.begin(), files.end(), [&](const auto& kv) { return kv.second == candidate; });
        if (!tracked && !candidate.exists())
            return candidate;
    }
    return userDir.getChildFile(stem + "-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()) + ext);
}

bool PresetManager::writePreset(const juce::File& target, const pre::Preset& preset) const
{
    if (!userDir.createDirectory().wasOk())
        return false;
    const auto text = pre::serializePreset(preset);
    juce::TemporaryFile temp(target);
    return temp.getFile().replaceWithData(text.data(), text.size()) && temp.overwriteTargetFileWithTemporary();
}

PresetManager::Result PresetManager::saveUser(pre::Preset preset, bool overwrite)
{
    preset.factory = false;
    preset.name = pre::sanitizeText(preset.name, pre::maxNameLength);
    const std::lock_guard<std::mutex> lock(mutex);
    const auto key = lowerKey(preset.name);
    const bool existed = library.indexOf(preset.name) >= static_cast<int>(library.factoryCount());
    const auto previous = existed ? std::optional<pre::Preset>(library.at(static_cast<std::size_t>(library.indexOf(preset.name))))
                                  : std::nullopt;
    if (const auto status = library.addUser(preset, overwrite); status != pre::LibraryStatus::Ok)
        return fail(status);

    const auto tracked = files.find(key);
    const auto target = tracked != files.end() ? tracked->second : fileForNewName(preset.name);
    if (!writePreset(target, preset))
    {
        // Roll back the in-memory change so the list matches the disk.
        if (previous)
            (void) library.addUser(*previous, true);
        else
            (void) library.removeUser(preset.name);
        return fail(pre::LibraryStatus::InvalidPreset, "The preset file could not be written to " + userDir.getFullPathName());
    }
    files[key] = target;
    Result r;
    r.index = library.indexOf(preset.name);
    return r;
}

PresetManager::Result PresetManager::renameUser(const juce::String& from, const juce::String& to)
{
    const std::lock_guard<std::mutex> lock(mutex);
    const auto oldName = from.toStdString();
    const auto newName = pre::sanitizeText(to.toStdString(), pre::maxNameLength);
    const int oldIndex = library.indexOf(oldName);
    if (oldIndex < 0)
        return fail(pre::LibraryStatus::NotFound);
    const auto oldKey = lowerKey(library.at(static_cast<std::size_t>(oldIndex)).name);
    const auto oldFileIt = files.find(oldKey);
    const auto oldFile = oldFileIt != files.end() ? oldFileIt->second : juce::File();
    const auto originalName = library.at(static_cast<std::size_t>(oldIndex)).name;
    if (const auto status = library.renameUser(oldName, newName); status != pre::LibraryStatus::Ok)
        return fail(status);

    const auto newIndex = library.indexOf(newName);
    auto updated = library.at(static_cast<std::size_t>(newIndex));
    const auto newKey = lowerKey(newName);
    const bool onlyCaseChanged = newKey == oldKey;
    const auto target = onlyCaseChanged && oldFile != juce::File() ? oldFile : fileForNewName(newName);
    if (!writePreset(target, updated))
    {
        (void) library.renameUser(newName, originalName);
        return fail(pre::LibraryStatus::InvalidPreset, "The preset file could not be written to " + userDir.getFullPathName());
    }
    if (oldFile != juce::File() && oldFile != target)
        oldFile.deleteFile();
    files.erase(oldKey);
    files[newKey] = target;
    Result r;
    r.index = newIndex;
    return r;
}

PresetManager::Result PresetManager::deleteUser(const juce::String& name)
{
    const std::lock_guard<std::mutex> lock(mutex);
    const auto key = lowerKey(name.toStdString());
    const auto it = files.find(key);
    const auto file = it != files.end() ? it->second : juce::File();
    if (const auto status = library.removeUser(name.toStdString()); status != pre::LibraryStatus::Ok)
        return fail(status);
    files.erase(key);
    if (file != juce::File() && file.existsAsFile() && !file.deleteFile())
        return fail(pre::LibraryStatus::InvalidPreset, "The preset was removed from the list, but its file could not be deleted: " + file.getFullPathName());
    return {};
}

PresetManager::Result PresetManager::importFile(const juce::File& file)
{
    pre::Preset preset;
    juce::String reason;
    if (!readPresetFile(file, preset, reason))
        return fail(pre::LibraryStatus::InvalidPreset, "Could not import " + file.getFileName() + ": " + reason);

    const std::lock_guard<std::mutex> lock(mutex);
    const auto base = preset.name;
    for (int n = 2; library.indexOf(preset.name) >= 0; ++n)
    {
        if (n > 999)
            return fail(pre::LibraryStatus::Exists);
        const auto suffix = " " + std::to_string(n);
        preset.name = base.substr(0, pre::maxNameLength - suffix.size()) + suffix;
    }
    if (const auto status = library.addUser(preset, false); status != pre::LibraryStatus::Ok)
        return fail(status);
    const auto target = fileForNewName(preset.name);
    if (!writePreset(target, preset))
    {
        (void) library.removeUser(preset.name);
        return fail(pre::LibraryStatus::InvalidPreset, "The preset file could not be written to " + userDir.getFullPathName());
    }
    files[lowerKey(preset.name)] = target;
    Result r;
    r.index = library.indexOf(preset.name);
    return r;
}

PresetManager::Result PresetManager::exportTo(const juce::String& name, const juce::File& destination) const
{
    const std::lock_guard<std::mutex> lock(mutex);
    const int index = library.indexOf(name.toStdString());
    if (index < 0)
        return fail(pre::LibraryStatus::NotFound);
    auto target = destination;
    if (!target.hasFileExtension(juce::String(std::string(pre::fileExtension))))
        target = target.withFileExtension(juce::String(std::string(pre::fileExtension)));
    const auto text = pre::serializePreset(library.at(static_cast<std::size_t>(index)));
    if (!target.replaceWithData(text.data(), text.size()))
        return fail(pre::LibraryStatus::InvalidPreset, "Could not write " + target.getFullPathName());
    Result r;
    r.index = index;
    return r;
}
