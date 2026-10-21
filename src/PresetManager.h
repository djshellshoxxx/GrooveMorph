#pragma once

#include <JuceHeader.h>

#include "core/preset/Preset.h"
#include "core/preset/PresetLibrary.h"

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// N02: factory + user preset list and the `.gmpreset` files on disk. All path logic lives here
// (the portable core only parses/serialises text). Thread-safe: host program queries may arrive from
// any non-audio thread, edits come from the message thread. Never called from the audio callback.
class PresetManager final
{
public:
    struct Result
    {
        groovemorph::core::preset::LibraryStatus status = groovemorph::core::preset::LibraryStatus::Ok;
        juce::String message; // empty on success
        int index = -1;       // combined-list index of the affected preset, when any
        [[nodiscard]] bool ok() const noexcept { return message.isEmpty() && status == groovemorph::core::preset::LibraryStatus::Ok; }
    };

    // <user application data>/djshellshoxxx/GrooveMorph/Presets
    static juce::File defaultDirectory();

    explicit PresetManager(juce::File userDirectory = defaultDirectory());

    [[nodiscard]] juce::File directory() const { return userDir; }
    void rescan(); // re-reads the user directory (hostile or oversize files are skipped)

    [[nodiscard]] int count() const;
    [[nodiscard]] int factoryCount() const;
    [[nodiscard]] bool isFactory(int index) const;
    [[nodiscard]] juce::String nameAt(int index) const;
    [[nodiscard]] std::optional<groovemorph::core::preset::Preset> presetAt(int index) const;
    [[nodiscard]] int indexOf(const juce::String& name) const;
    [[nodiscard]] std::vector<std::size_t> filter(const groovemorph::core::preset::Filter& f) const;
    [[nodiscard]] juce::StringArray categories() const;
    [[nodiscard]] juce::StringArray allTags() const;

    // New user preset, or replace the one of the same name when `overwrite`. Factory names are refused.
    Result saveUser(groovemorph::core::preset::Preset preset, bool overwrite);
    Result renameUser(const juce::String& from, const juce::String& to);
    Result deleteUser(const juce::String& name);
    // Validates and re-serialises (never copies raw bytes). A clashing name gets " 2", " 3" ... appended.
    Result importFile(const juce::File& file);
    Result exportTo(const juce::String& name, const juce::File& destination) const;

private:
    juce::File fileForNewName(const std::string& name) const;
    bool writePreset(const juce::File& target, const groovemorph::core::preset::Preset& preset) const;

    juce::File userDir;
    mutable std::mutex mutex;
    groovemorph::core::preset::PresetLibrary library;
    std::map<std::string, juce::File> files; // lower-case user preset name -> file
};
