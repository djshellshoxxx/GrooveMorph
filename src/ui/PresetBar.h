#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

class GrooveMorphAudioProcessor;

namespace groovemorph::ui
{
// N02 preset bar: previous / next, a searchable dropdown grouped by category with a tag filter,
// Save / Save As / Rename / Delete and Import / Export. All state lives in the processor
// (GrooveMorphAudioProcessor::applyPreset etc.); this component only drives it.
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar(GrooveMorphAudioProcessor& processor);
    ~PresetBar() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    // Cheap; call from the editor's timer so the name, modified marker and button states follow the model.
    void refresh();

    // The browser dropdown's content (also used by the snapshot tool to render the open state).
    std::unique_ptr<juce::Component> createBrowserContent();

    // Called on the message thread after a preset has been loaded, imported or deleted.
    std::function<void()> onChanged;

private:
    void stepPreset(int direction);
    void load(int index);
    void showBrowser();
    void save();
    void promptSaveAs();
    void saveNamed(const juce::String& name, const juce::String& category, const juce::StringArray& tags, bool overwrite);
    void promptRename();
    void confirmDelete();
    void importPreset();
    void exportPreset();
    void report(const juce::String& message);
    void changed();

    GrooveMorphAudioProcessor& processor;
    juce::TextButton previousButton{"<"}, nextButton{">"}, nameButton, saveButton{"Save"}, saveAsButton{"Save As"},
        renameButton{"Rename"}, deleteButton{"Delete"}, importButton{"Import"}, exportButton{"Export"};
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBar)
};
} // namespace groovemorph::ui
