#include "ui/PresetBar.h"
#include "ui/theme/Theme.h"

#include "PluginProcessor.h"

#include <vector>

namespace groovemorph::ui
{
namespace
{
const juce::Colour barBackground = theme::surface;
const juce::Colour buttonColour = theme::surfaceRaised;
const juce::Colour accentColour = theme::accent;
const juce::Colour modifiedColour = theme::queued; // paired with the "*" after the name

juce::StringArray splitTags(const juce::String& text)
{
    juce::StringArray tags;
    tags.addTokens(text, ",", "");
    tags.trim();
    tags.removeEmptyStrings();
    return tags;
}

// Popup content: search box, tag filter and a category-grouped list.
class BrowserPanel final : public juce::Component, private juce::ListBoxModel
{
public:
    BrowserPanel(PresetManager& managerToUse, juce::String currentName, std::function<void(int)> chooseCallback)
        : manager(managerToUse), current(std::move(currentName)), onChoose(std::move(chooseCallback))
    {
        search.setTextToShowWhenEmpty("Search presets...", theme::textDim);
        search.onTextChange = [this] { rebuild(); };
        addAndMakeVisible(search);

        tagBox.addItem("All tags", 1);
        tags = manager.allTags();
        for (int i = 0; i < tags.size(); ++i)
            tagBox.addItem(tags[i], i + 2);
        tagBox.setSelectedId(1, juce::dontSendNotification);
        tagBox.onChange = [this] { rebuild(); };
        addAndMakeVisible(tagBox);

        list.setModel(this);
        list.setRowHeight(24);
        addAndMakeVisible(list);
        setSize(400, 440);
        rebuild();
    }

    ~BrowserPanel() override { list.setModel(nullptr); }

    void paint(juce::Graphics& g) override { g.fillAll(barBackground); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto top = area.removeFromTop(28);
        tagBox.setBounds(top.removeFromRight(130));
        top.removeFromRight(6);
        search.setBounds(top);
        area.removeFromTop(6);
        list.setBounds(area);
    }

    void parentHierarchyChanged() override
    {
        if (isShowing())
            search.grabKeyboardFocus();
    }

private:
    struct Row
    {
        bool header = false;
        int index = -1; // combined-list index for items
        juce::String text;
    };

    void rebuild()
    {
        rows.clear();
        groovemorph::core::preset::Filter filter;
        filter.search = search.getText().toStdString();
        if (tagBox.getSelectedId() > 1)
            filter.tag = tags[tagBox.getSelectedId() - 2].toStdString();
        for (const auto& category : manager.categories())
        {
            filter.category = category.toStdString();
            const auto hits = manager.filter(filter);
            if (hits.empty())
                continue;
            rows.push_back({true, -1, category});
            for (const auto index : hits)
                rows.push_back({false, static_cast<int>(index), manager.nameAt(static_cast<int>(index))});
        }
        list.updateContent();
        list.repaint();
    }

    int getNumRows() override { return static_cast<int>(rows.size()); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= static_cast<int>(rows.size()))
            return;
        const auto& r = rows[static_cast<std::size_t>(row)];
        if (r.header)
        {
            g.fillAll(theme::surfaceRaised);
            g.setColour(accentColour);
            g.setFont(theme::font(theme::fontLabel, true));
            g.drawText(r.text.toUpperCase(), 8, 0, width - 16, height, juce::Justification::centredLeft);
            return;
        }
        if (selected)
        {
            g.fillAll(theme::surfaceHover);
            g.setColour(accentColour);
            g.fillRect(0, 0, 3, height);
        }
        const bool isCurrent = r.text.equalsIgnoreCase(current);
        g.setColour(isCurrent ? accentColour : theme::text);
        g.setFont(theme::font(theme::fontValue));
        g.drawText(r.text, 18, 0, width - 80, height, juce::Justification::centredLeft);
        if (!manager.isFactory(r.index))
        {
            g.setColour(theme::textDim);
            g.setFont(theme::font(theme::fontLabel));
            g.drawText("user", width - 58, 0, 50, height, juce::Justification::centredRight);
        }
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override { choose(row); }
    void returnKeyPressed(int row) override { choose(row); }

    void choose(int row)
    {
        if (row < 0 || row >= static_cast<int>(rows.size()) || rows[static_cast<std::size_t>(row)].header)
            return;
        const int index = rows[static_cast<std::size_t>(row)].index;
        const auto callback = onChoose;
        if (callback)
            callback(index);
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    PresetManager& manager;
    juce::String current;
    std::function<void(int)> onChoose;
    juce::TextEditor search;
    juce::ComboBox tagBox;
    juce::StringArray tags;
    juce::ListBox list{"presets"};
    std::vector<Row> rows;
};
} // namespace

PresetBar::PresetBar(GrooveMorphAudioProcessor& processorToUse) : processor(processorToUse)
{
    for (auto* button : {&previousButton, &nextButton, &nameButton, &saveButton, &saveAsButton, &renameButton,
                         &deleteButton, &importButton, &exportButton})
    {
        button->setColour(juce::TextButton::buttonColourId, buttonColour);
        button->setColour(juce::TextButton::textColourOffId, theme::text);
        addAndMakeVisible(*button);
    }
    previousButton.setTooltip("Load the previous preset in the list. Loading is one Undo step.");
    nextButton.setTooltip("Load the next preset in the list. Loading is one Undo step.");
    nameButton.setTooltip("Open the preset browser: search, filter by tag, grouped by category. A * after the name means the scenes were changed after loading.");
    saveButton.setTooltip("Save the four scenes over the loaded user preset. On a factory preset (or with none loaded) this works like Save As.");
    saveAsButton.setTooltip("Save the four scenes as a new user preset with a name, category and tags.");
    renameButton.setTooltip("Rename the loaded user preset. Factory presets cannot be renamed.");
    deleteButton.setTooltip("Delete the loaded user preset file. Factory presets cannot be deleted.");
    importButton.setTooltip("Import a .gmpreset file into your user presets.");
    exportButton.setTooltip("Export the loaded preset to a .gmpreset file you can share.");

    previousButton.onClick = [this] { stepPreset(-1); };
    nextButton.onClick = [this] { stepPreset(1); };
    nameButton.onClick = [this] { showBrowser(); };
    saveButton.onClick = [this] { save(); };
    saveAsButton.onClick = [this] { promptSaveAs(); };
    renameButton.onClick = [this] { promptRename(); };
    deleteButton.onClick = [this] { confirmDelete(); };
    importButton.onClick = [this] { importPreset(); };
    exportButton.onClick = [this] { exportPreset(); };
    refresh();
}

PresetBar::~PresetBar() = default;

void PresetBar::paint(juce::Graphics& g)
{
    g.setColour(barBackground);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), theme::radius);
}

void PresetBar::resized()
{
    auto area = getLocalBounds().reduced(4);
    const auto place = [&area](juce::Component& c, int width) {
        c.setBounds(area.removeFromLeft(width));
        area.removeFromLeft(4);
    };
    place(previousButton, 32);
    const int fixedRight = 62 + 66 + 66 + 62 + 62 + 62 + 6 * 4 + 32 + 4;
    place(nameButton, juce::jmax(120, area.getWidth() - fixedRight));
    place(nextButton, 32);
    place(saveButton, 62);
    place(saveAsButton, 66);
    place(renameButton, 66);
    place(deleteButton, 62);
    place(importButton, 62);
    place(exportButton, 62);
}

void PresetBar::refresh()
{
    const auto name = processor.getCurrentPresetName();
    const bool modified = processor.isPresetModified();
    juce::String text = name.isEmpty() ? juce::String("Presets: choose...") : name;
    if (name.isNotEmpty() && modified)
        text << " *";
    nameButton.setButtonText(text);
    nameButton.setColour(juce::TextButton::textColourOffId,
                         name.isNotEmpty() && modified ? modifiedColour : theme::text);
    const auto& manager = processor.getPresetManager();
    const int index = name.isEmpty() ? -1 : manager.indexOf(name);
    const bool user = index >= 0 && !manager.isFactory(index);
    deleteButton.setEnabled(user);
    renameButton.setEnabled(user);
    exportButton.setEnabled(index >= 0);
    saveButton.setButtonText(user ? "Save" : "Save...");
}

void PresetBar::changed()
{
    refresh();
    if (onChanged)
        onChanged();
}

void PresetBar::report(const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Preset", message, "OK", this);
}

void PresetBar::load(int index)
{
    if (!processor.applyPreset(index))
    {
        report(processor.isAuditioning() ? "Finish or revert the preview before loading a preset."
                                         : "That preset could not be loaded.");
        return;
    }
    changed();
}

void PresetBar::stepPreset(int direction)
{
    const int total = processor.getPresetManager().count();
    if (total <= 0)
        return;
    const auto name = processor.getCurrentPresetName();
    const int current = name.isEmpty() ? -1 : processor.getPresetManager().indexOf(name);
    int next;
    if (current < 0)
        next = direction > 0 ? 0 : total - 1;
    else
        next = (current + direction + total) % total;
    load(next);
}

std::unique_ptr<juce::Component> PresetBar::createBrowserContent()
{
    juce::Component::SafePointer<PresetBar> safe(this);
    return std::make_unique<BrowserPanel>(processor.getPresetManager(), processor.getCurrentPresetName(),
                                          [safe](int index) {
                                              if (safe != nullptr)
                                                  safe->load(index);
                                          });
}

void PresetBar::showBrowser()
{
    juce::CallOutBox::launchAsynchronously(createBrowserContent(), nameButton.getScreenBounds(), nullptr);
}

void PresetBar::saveNamed(const juce::String& name, const juce::String& category, const juce::StringArray& tags,
                          bool overwrite)
{
    const auto result = processor.saveCurrentAsPreset(name, category, tags, overwrite);
    if (result.ok())
    {
        changed();
        return;
    }
    if (result.status == groovemorph::core::preset::LibraryStatus::Exists && !overwrite)
    {
        juce::Component::SafePointer<PresetBar> safe(this);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::QuestionIcon, "Replace preset?",
                                                         "A user preset named \"" + name + "\" already exists. Replace it?",
                                                         "Replace", "Cancel", this),
            [safe, name, category, tags](int answer) {
                if (safe != nullptr && answer == 1)
                    safe->saveNamed(name, category, tags, true);
            });
        return;
    }
    report(result.message);
}

void PresetBar::save()
{
    const auto name = processor.getCurrentPresetName();
    auto& manager = processor.getPresetManager();
    const int index = name.isEmpty() ? -1 : manager.indexOf(name);
    if (index < 0 || manager.isFactory(index))
    {
        promptSaveAs();
        return;
    }
    // Overwrite the loaded user preset, keeping its category and tags.
    const auto stored = manager.presetAt(index);
    juce::StringArray tags;
    juce::String category;
    if (stored)
    {
        category = juce::String(stored->category);
        for (const auto& tag : stored->tags)
            tags.add(juce::String(tag));
    }
    saveNamed(name, category, tags, true);
}

void PresetBar::promptSaveAs()
{
    const auto current = processor.getCurrentPresetName();
    auto* window = new juce::AlertWindow("Save preset", "Saves the four scenes as a user preset.",
                                         juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", current.isEmpty() ? juce::String() : current + " copy", "Name");
    window->addTextEditor("category", "User", "Category");
    window->addTextEditor("tags", {}, "Tags (comma separated)");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> dialog(window);
    juce::Component::SafePointer<PresetBar> safe(this);
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([dialog, safe](int result) {
                                if (result != 1 || dialog == nullptr || safe == nullptr)
                                    return;
                                safe->saveNamed(dialog->getTextEditorContents("name").trim(),
                                                dialog->getTextEditorContents("category").trim(),
                                                splitTags(dialog->getTextEditorContents("tags")), false);
                            }),
                            true);
}

void PresetBar::promptRename()
{
    const auto current = processor.getCurrentPresetName();
    if (current.isEmpty())
        return;
    auto* window = new juce::AlertWindow("Rename preset", "Factory presets cannot be renamed.",
                                         juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", current, "New name");
    window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> dialog(window);
    juce::Component::SafePointer<PresetBar> safe(this);
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([dialog, safe, current](int result) {
                                if (result != 1 || dialog == nullptr || safe == nullptr)
                                    return;
                                const auto outcome = safe->processor.getPresetManager().renameUser(
                                    current, dialog->getTextEditorContents("name").trim());
                                if (!outcome.ok())
                                {
                                    safe->report(outcome.message);
                                    return;
                                }
                                safe->processor.setCurrentPresetName(safe->processor.getPresetManager().nameAt(outcome.index));
                                safe->changed();
                            }),
                            true);
}

void PresetBar::confirmDelete()
{
    const auto name = processor.getCurrentPresetName();
    if (name.isEmpty())
        return;
    juce::Component::SafePointer<PresetBar> safe(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::WarningIcon, "Delete preset?",
                                                     "Delete the user preset \"" + name + "\" and its file? The current scenes stay as they are.",
                                                     "Delete", "Cancel", this),
        [safe, name](int answer) {
            if (safe == nullptr || answer != 1)
                return;
            const auto outcome = safe->processor.getPresetManager().deleteUser(name);
            if (!outcome.ok())
                safe->report(outcome.message);
            safe->processor.setCurrentPresetName({});
            safe->changed();
        });
}

void PresetBar::importPreset()
{
    chooser = std::make_unique<juce::FileChooser>("Import a GrooveMorph preset", juce::File{}, "*.gmpreset");
    juce::Component::SafePointer<PresetBar> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safe](const juce::FileChooser& fc) {
                             const auto file = fc.getResult();
                             if (safe == nullptr || file == juce::File{})
                                 return;
                             const auto outcome = safe->processor.getPresetManager().importFile(file);
                             if (!outcome.ok())
                             {
                                 safe->report(outcome.message);
                                 return;
                             }
                             safe->load(outcome.index);
                         });
}

void PresetBar::exportPreset()
{
    const auto name = processor.getCurrentPresetName();
    if (name.isEmpty())
        return;
    chooser = std::make_unique<juce::FileChooser>(
        "Export preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name + ".gmpreset"),
        "*.gmpreset");
    juce::Component::SafePointer<PresetBar> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [safe, name](const juce::FileChooser& fc) {
                             const auto file = fc.getResult();
                             if (safe == nullptr || file == juce::File{})
                                 return;
                             const auto outcome = safe->processor.getPresetManager().exportTo(name, file);
                             if (!outcome.ok())
                                 safe->report(outcome.message);
                         });
}
} // namespace groovemorph::ui
