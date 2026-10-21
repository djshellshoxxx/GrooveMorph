#include "ui/shell/TimingStrip.h"

#include "PluginProcessor.h"
#include "ui/shell/ShellUtil.h"

#include <algorithm>

namespace groovemorph::ui::shell
{
namespace theme = groovemorph::ui::theme;

TimingStrip::TimingStrip(GrooveMorphAudioProcessor& p) : processor(p)
{
    for (auto* label : {&timingLabel, &statusLabel})
    {
        label->setFont(theme::font(theme::fontLabel));
        label->setColour(juce::Label::textColourId, theme::text);
        addAndMakeVisible(*label);
    }
    phraseTip.setTooltip("The phrase: one box per bar. The bright line is the current position inside the phrase.");
    addAndMakeVisible(phraseTip);
    describe(timingButton, "Timing", "Opens the timing panel: manual tempo, beats per bar, beat unit, timing method and phrase length.");
    timingButton.setWantsKeyboardFocus(true);
    addAndMakeVisible(timingButton);
}

TimingStrip::~TimingStrip() = default;

juce::Rectangle<int> TimingStrip::cursorRect(int x) const
{
    return {x - 1, barArea.getY(), 4, barArea.getHeight()};
}

void TimingStrip::tick()
{
    const int newBars = std::max(1, static_cast<int>(processor.parameters.getRawParameterValue("phraseBars")->load()));
    int newX = -1;
    if (processor.hasValidTiming() && barArea.getWidth() > 2)
    {
        const double phase = std::clamp(processor.getBarInPhrase() / static_cast<double>(newBars), 0.0, 1.0);
        newX = barArea.getX() + static_cast<int>(phase * static_cast<double>(barArea.getWidth() - 2));
    }
    if (newBars != bars)
    {
        bars = newBars;
        cursorX = newX;
        repaint(barArea);
        return;
    }
    if (newX != cursorX)
    {
        if (cursorX >= 0) repaint(cursorRect(cursorX));
        if (newX >= 0) repaint(cursorRect(newX));
        cursorX = newX;
    }
}

void TimingStrip::paint(juce::Graphics& g)
{
    paintPanel(g, getLocalBounds());
    if (barArea.isEmpty())
        return;
    const auto bar = barArea.toFloat();
    g.setColour(theme::surfaceRaised);
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(theme::strokeStrong);
    const float cell = bar.getWidth() / static_cast<float>(bars);
    for (int i = 1; i < bars; ++i)
        g.drawVerticalLine(static_cast<int>(bar.getX() + cell * static_cast<float>(i)), bar.getY(), bar.getBottom());
    if (cell >= 18.0f)
    {
        g.setColour(theme::textDim);
        g.setFont(theme::font(theme::fontCaption));
        for (int i = 0; i < bars; ++i)
            g.drawText(juce::String(i + 1), static_cast<int>(bar.getX() + cell * static_cast<float>(i)) + 3, barArea.getY(),
                       static_cast<int>(cell) - 3, barArea.getHeight(), juce::Justification::centredLeft, false);
    }
    if (cursorX >= 0)
    {
        g.setColour(theme::accent);
        g.fillRect(static_cast<float>(cursorX), bar.getY(), 2.0f, bar.getHeight());
    }
}

void TimingStrip::resized()
{
    auto area = getLocalBounds().reduced(theme::space2, 3);
    timingButton.setBounds(area.removeFromRight(88));
    area.removeFromRight(theme::space2);
    barArea = area.removeFromLeft(std::clamp(area.getWidth() / 5, 120, 360));
    phraseTip.setBounds(barArea);
    area.removeFromLeft(theme::space2);
    timingLabel.setBounds(area.removeFromLeft(area.getWidth() * 2 / 5));
    statusLabel.setBounds(area);
    cursorX = -1;
    tick();
}
} // namespace groovemorph::ui::shell
