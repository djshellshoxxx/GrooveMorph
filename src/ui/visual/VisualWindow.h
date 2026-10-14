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
    explicit VisualContent(GrooveMorphAudioProcessor& processor);
    ~VisualContent() override;

    // Starts/stops the 30 Hz refresh and the trace ring (no tracing cost while inactive).
    void setActive(bool active);
    // One refresh from the ring and the processor (the timer calls this; tools and tests call it directly).
    void refresh();
    [[nodiscard]] juce::String ledgerText() const { return ledgerView.asText(); }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { refresh(); }

    GrooveMorphAudioProcessor& processor;
    WeaveView weaveView;
    LedgerView ledgerView;
    RingsView ringsView;
    StationsView stationsView;
    groovemorph::core::viz::LapRecorder lapRecorder;
    int lastPhraseBeats = 0;
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
