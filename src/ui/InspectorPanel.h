#pragma once

#include <JuceHeader.h>

#include "core/OperationParams.h"

#include <functional>
#include <memory>

class GrooveMorphAudioProcessor;

namespace groovemorph::ui
{
// What the inspector needs from the editor. All callbacks run on the message thread.
struct SceneEditAccess
{
    std::function<std::size_t()> editingIndex;
    // Settings of the scene being edited (preview working copy while auditioning).
    std::function<groovemorph::core::VariationSettings()> settings;
    // Routes through the editor's single edit path: preview edit while auditioning, otherwise one
    // undoable committed edit.
    std::function<void(const groovemorph::core::VariationSettings&)> setSettings;
    // Settings stored in the bank for the edited scene (the generator's source).
    std::function<groovemorph::core::VariationSettings()> storedSettings;
    // Starts or updates a preview with candidate settings; nothing is committed.
    std::function<void(const groovemorph::core::VariationSettings&)> previewCandidate;
    std::function<bool()> previewing;
};

// Tabbed advanced-controls inspector: Placement, Generator, Scenes, Morph/Return, Limits, Status.
class InspectorPanel final : public juce::Component
{
public:
    InspectorPanel(GrooveMorphAudioProcessor& processor, SceneEditAccess access);
    ~InspectorPanel() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    // Re-read scene/processor state into the widgets (editor timer, ~10 Hz).
    void refresh();
    void showTab(int index);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace groovemorph::ui
