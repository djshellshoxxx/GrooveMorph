#include "ui/shell/TimingPanel.h"

#include "PluginProcessor.h"
#include "ui/shell/ShellUtil.h"

namespace groovemorph::ui::shell
{
namespace theme = groovemorph::ui::theme;

TimingPanel::TimingPanel(GrooveMorphAudioProcessor& processor)
{
    setWantsKeyboardFocus(true);
    const auto slider = [&](groovemorph::ui::MenuSlider& s, const char* parameter, const juce::String& name, const juce::String& tip) {
        configureLinear(s, name);
        bindSlider(processor, *this, s, sliderAttachments, parameter, name);
        describe(s, name, tip);
    };
    slider(tempoSlider, "manualTempo", "Manual tempo (BPM)",
           "Tempo in beats per minute (20-300), used when the host does not provide one and in Standalone. Changing it keeps the current position (no jump). Imported loops follow it: they play at original speed when it equals the loop's own tempo.");
    slider(meterSlider, "manualMeterNumerator", "Manual beats per bar",
           "Beats per bar (1-16) used when the host does not provide a time signature. 4 means 4/4 or 4/8, 3 means 3/4 and so on. The beat unit box sets the note value of one beat.");
    slider(phraseSlider, "phraseBars", "Phrase length (bars)",
           "Length of one phrase in bars (1-16). Scene changes can wait for a phrase boundary; placement windows, the phrase rings and morph/return timing follow this length.");
    tempoSlider.setNumDecimalPlacesToDisplay(2);
    meterSlider.setNumDecimalPlacesToDisplay(0);
    phraseSlider.setNumDecimalPlacesToDisplay(0);
    tempoSlider.info().unit = "BPM";

    bindCombo(processor, *this, timingMode, comboAttachments, "timingMode", {"Automatic", "Host PPQ", "Sample Clock"}, "Timing method");
    timingMode.setTooltip("Automatic prefers host PPQ and falls back to samples. Host PPQ holds scheduled switches when PPQ is missing. Sample Clock free-runs without a host; a stopped host stays stopped.");
    bindCombo(processor, *this, denominator, comboAttachments, "manualMeterDenominator", {"1", "2", "4", "8", "16"}, "Manual beat unit");
    denominator.setTooltip("Note value of one beat for the manual time signature: 4 = quarter note, 8 = eighth note, and so on. Used only when the host provides no time signature.");
    for (auto* c : std::initializer_list<juce::Component*>{&timingMode, &denominator})
    {
        c->setTitle(c->getName());
        c->setDescription(dynamic_cast<juce::SettableTooltipClient*>(c)->getTooltip());
    }

    describe(closeButton, "Close timing panel", "Closes the timing panel. Shortcut: Esc.");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeButton);
    setSize(preferredWidth, preferredHeight);
}

TimingPanel::~TimingPanel() = default;

void TimingPanel::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme::surfaceRaised);
    g.fillRoundedRectangle(r, theme::radiusLarge);
    g.setColour(theme::strokeStrong);
    g.drawRoundedRectangle(r, theme::radiusLarge, 1.0f);
    auto area = getLocalBounds().reduced(theme::space3);
    g.setColour(theme::text);
    g.setFont(theme::font(theme::fontTitle, true));
    g.drawText("Timing", area.removeFromTop(24), juce::Justification::centredLeft, false);
    const juce::StringArray labels{"Timing method", "Manual tempo", "Beats per bar", "Beat unit", "Phrase bars"};
    // Row geometry mirrors resized().
    int y = area.getY() + 4;
    for (int i = 0; i < labels.size(); ++i)
    {
        paintCaption(g, labels[i], {area.getX(), y, 120, theme::minTarget});
        y += theme::minTarget + 4;
    }
}

void TimingPanel::resized()
{
    auto area = getLocalBounds().reduced(theme::space3);
    area.removeFromTop(24 + 4);
    const auto row = [&area]() {
        auto r = area.removeFromTop(theme::minTarget);
        area.removeFromTop(4);
        r.removeFromLeft(124);
        return r;
    };
    timingMode.setBounds(row());
    tempoSlider.setBounds(row());
    meterSlider.setBounds(row());
    denominator.setBounds(row().removeFromLeft(100));
    phraseSlider.setBounds(row());
    closeButton.setBounds(getLocalBounds().reduced(theme::space3).removeFromTop(28).removeFromRight(72));
}

bool TimingPanel::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }
    return false;
}
} // namespace groovemorph::ui::shell
