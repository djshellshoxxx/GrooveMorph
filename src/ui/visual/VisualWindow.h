#pragma once

#include <JuceHeader.h>

#include "core/scene/SceneBank.h"
#include "core/trace/TraceFrame.h"
#include "ui/visual/LedgerView.h"
#include "ui/visual/RingsView.h"
#include "ui/visual/StationsView.h"
#include "core/viz/LapRecorder.h"
#include "ui/visual/WeaveView.h"

#include <functional>
#include <memory>
#include <vector>

class GrooveMorphAudioProcessor;

namespace groovemorph::ui::visual
{
// Pulls trace frames and processor status at 30 Hz and hands geometry/text to the views.
// All work runs on the message thread; the audio thread only writes the TraceRing.
class VisualContent final : public juce::Component, private juce::Timer
{
public:
    // persistPreferences: remember Reduce motion and the time span across sessions (off in tests and tools).
    explicit VisualContent(GrooveMorphAudioProcessor& processor, bool persistPreferences = false);
    ~VisualContent() override;

    // Starts/stops the 30 Hz refresh and the trace ring (no tracing cost while inactive).
    void setActive(bool active);
    // One refresh from the ring and the processor (the timer calls this; tools and tests call it directly).
    void refresh();
    [[nodiscard]] juce::String ledgerText() const { return ledgerView.asText(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void copyLedgerToClipboard();
    void setReduceMotion(bool on);
    [[nodiscard]] bool reduceMotion() const noexcept { return reduceMotionOn; }
    void setSpanBeats(double beats);
    [[nodiscard]] double spanBeats() const noexcept { return viewSpan; }
    std::function<void()> onEscape;

private:
    void timerCallback() override { refresh(); }
    void savePreferences();
    int refreshRate() const noexcept { return reduceMotionOn ? 10 : 30; }

    GrooveMorphAudioProcessor& processor;
    WeaveView weaveView;
    LedgerView ledgerView;
    RingsView ringsView;
    StationsView stationsView;
    groovemorph::core::viz::LapRecorder lapRecorder;
    int lastPhraseBeats = 0;
    juce::TextButton copyButton{"Copy as text"};
    juce::ToggleButton reduceMotionButton{"Reduce motion"};
    juce::Label spanLabel;
    juce::ComboBox spanBox;
    std::unique_ptr<juce::PropertiesFile> preferences;
    bool reduceMotionOn = false;
    double viewSpan = 4.0;
    bool loadingPreferences = false;
    SceneOrbitView orbitView;
    std::vector<groovemorph::core::trace::TraceFrame> frameBuffer;
    groovemorph::core::scene::SceneBank cachedBank;
    juce::uint32 bankFetchedAt = 0;
    bool active = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualContent)
};

// Separate, optional window (opened/closed from the main editor's Visual button). Closing it stops the
// refresh and deactivates tracing.
class VisualWindow final : public juce::DocumentWindow
{
public:
    VisualWindow(GrooveMorphAudioProcessor& processor, std::function<void()> onClose);
    ~VisualWindow() override;
    void open();
    void close();
    void closeButtonPressed() override;
    [[nodiscard]] VisualContent& content() { return *contentComponent; }

private:
    VisualContent* contentComponent = nullptr; // owned by the window
    std::function<void()> onCloseRequested;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualWindow)
};
} // namespace groovemorph::ui::visual
