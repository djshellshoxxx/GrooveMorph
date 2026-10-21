#pragma once

#include <JuceHeader.h>

#include "core/OperationParams.h"
#include "core/control/EuclidPattern.h"
#include "ui/InspectorPanel.h"

class GrooveMorphAudioProcessor;

namespace groovemorph::ui
{
// N03: Euclidean pattern generator and per-event / per-lane chance (docs/specs/N03-EUCLID-CHANCE.md).
// Embedded in the Place tab. All scene changes go through SceneEditAccess, so locks, preview/audition and
// undo behave exactly like every other Place edit. Message thread only.
class EuclidPanel final : public juce::Component
{
public:
    EuclidPanel(GrooveMorphAudioProcessor& processor, SceneEditAccess access);

    // Re-read the scene (editor timer). selectedEvent = Place canvas selection, or -1.
    void refresh(const groovemorph::core::VariationSettings& settings, int selectedEvent);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Row
    {
        juce::Label label;
        juce::Slider slider;
        bool dragging = false;
    };

    void setupRow(Row& row, const juce::String& text, const juce::String& tip, double lo, double hi, double step,
                  int decimals, const juce::String& suffix = {});
    [[nodiscard]] groovemorph::core::control::EuclidParams params() const;
    void syncPulseRange();
    void showReport(const groovemorph::core::control::EuclidReport& report, const juce::String& verb);
    void doPreview();
    void doApply();
    void doClear();
    void commitEventChance();
    void commitLaneChance();
    [[nodiscard]] std::uint8_t targetOperation() const;

    GrooveMorphAudioProcessor& processor;
    SceneEditAccess access;
    bool refreshing = false;
    int selected = -1;
    bool selectedLocked = false;

    juce::ComboBox operationBox, gridBox, gateBox;
    Row steps, pulses, rotation, accentEvery, density, eventChance, laneChance;
    juce::TextButton previewButton, applyButton, clearButton;
    juce::Label status;
    juce::Rectangle<int> ringBounds;
};
} // namespace groovemorph::ui
