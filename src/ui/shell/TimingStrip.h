#pragma once

#include <JuceHeader.h>

class GrooveMorphAudioProcessor;

namespace groovemorph::ui::shell
{
// Thin strip under the performance band: the painted phrase cursor (one box per bar), the timing text,
// the status text and the "Timing..." button that opens the TimingPanel.
class TimingStrip final : public juce::Component
{
public:
    static constexpr int height = 28;

    explicit TimingStrip(GrooveMorphAudioProcessor& processor);
    ~TimingStrip() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // Reads bar count and position; repaints only the strip of the old and new cursor column (and all of it
    // when the bar count changes). Cheap; called at 30 Hz.
    void tick();

    juce::Label timingLabel, statusLabel;
    juce::TextButton timingButton{"Timing..."};

private:
    struct TipArea final : juce::Component, juce::SettableTooltipClient {};
    juce::Rectangle<int> cursorRect(int x) const;

    GrooveMorphAudioProcessor& processor;
    TipArea phraseTip;
    juce::Rectangle<int> barArea;
    int bars = 4;
    int cursorX = -1; // -1 = no valid timing
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimingStrip)
};
} // namespace groovemorph::ui::shell
