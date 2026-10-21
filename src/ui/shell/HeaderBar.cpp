#include "ui/shell/HeaderBar.h"

#include "PluginProcessor.h"
#include "ui/shell/ShellUtil.h"

namespace groovemorph::ui::shell
{
namespace theme = groovemorph::ui::theme;

void GearButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(down ? theme::accentDim : highlighted ? theme::surfaceHover : theme::surfaceRaised);
    g.fillRoundedRectangle(b, theme::radius);
    g.setColour(theme::stroke);
    g.drawRoundedRectangle(b, theme::radius, 1.0f);
    const auto c = b.getCentre();
    juce::Path gear;
    gear.addStar(c, 8, 6.0f, 9.0f, 0.0f);
    gear.setUsingNonZeroWinding(false);
    gear.addEllipse(c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
    g.setColour(theme::text);
    g.fillPath(gear);
    if (hasKeyboardFocus(false))
    {
        g.setColour(theme::focusRing);
        g.drawRoundedRectangle(b, theme::radius, 2.0f);
    }
}

HeaderBar::HeaderBar(GrooveMorphAudioProcessor& processor) : presetBar(processor)
{
    wordmark.setText("GrooveMorph", juce::dontSendNotification);
    wordmark.setFont(theme::font(theme::fontWordmark, true));
    wordmark.setColour(juce::Label::textColourId, theme::text);
    wordmark.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(wordmark);
    addAndMakeVisible(presetBar);

    describe(playButton, "Play",
             "Starts the internal transport from the beginning (Standalone, or Sample Clock timing without a host play state). Shortcut: Space.");
    describe(stopButton, "Stop",
             "Stops the internal transport and returns to the beginning. Effects, the demo loop and loaded loops stop with it. Shortcut: Space.");
    playButton.setClickingTogglesState(false);
    stopButton.setClickingTogglesState(false);
    playButton.setColour(juce::TextButton::buttonOnColourId, theme::sounding);
    stopButton.setColour(juce::TextButton::buttonOnColourId, theme::queued);
    describe(easyButton, "Easy mode", "Switches to Easy mode: one compact screen with the four scenes, Surprise me and the main macros.");
    easyButton.setDescription("Switch to the compact Easy mode view");
    describe(visualButton, "Visual",
             "Opens or closes the separate Visual window: a live picture of what the plugin is doing to the sound (which past audio you are hearing, what is planned next, gate, scenes) with the numbers behind it. Tracing only runs while the window is open.");
    describe(bypassButton, "Bypass", "Bypass: passes the sound through unchanged (a short fade avoids clicks). Host-automatable.");
    bypassButton.info() = {&processor, "Bypass", "p:bypass", {}};
    describe(settingsButton, "Settings",
             "Settings menu: Demo loop, Reset to defaults, UI scale, Show guide and Help. Shortcut for Help: F1.");
    for (auto* c : std::initializer_list<juce::Component*>{&playButton, &stopButton, &easyButton, &visualButton, &bypassButton, &settingsButton})
    {
        c->setWantsKeyboardFocus(true);
        addAndMakeVisible(*c);
    }
}

HeaderBar::~HeaderBar() = default;

void HeaderBar::paint(juce::Graphics& g)
{
    paintPanel(g, getLocalBounds());
}

void HeaderBar::resized()
{
    auto area = getLocalBounds().reduced(theme::space2, 0);
    const bool compact = getWidth() < 1100;
    const int buttonHeight = theme::minTarget;
    const int y = (getHeight() - buttonHeight) / 2;
    const int gap = compact ? 4 : theme::space2;
    // Right cluster, built from the right edge.
    const auto placeRight = [&](juce::Component& c, int width) {
        c.setBounds(area.removeFromRight(width).withY(y).withHeight(buttonHeight));
        area.removeFromRight(gap);
    };
    placeRight(settingsButton, buttonHeight);
    placeRight(bypassButton, compact ? 72 : 84);
    placeRight(visualButton, compact ? 62 : 72);
    placeRight(easyButton, compact ? 52 : 64);
    placeRight(stopButton, compact ? 46 : 56);
    placeRight(playButton, compact ? 46 : 56);
    if (!compact)
    {
        wordmark.setVisible(true);
        wordmark.setBounds(area.removeFromLeft(130).withY(y).withHeight(buttonHeight));
    }
    else
    {
        wordmark.setVisible(false);
    }
    // The preset bar needs about 600 px; it is 40 px high, centred.
    presetBar.setBounds(area.withY((getHeight() - 40) / 2).withHeight(40));
}
} // namespace groovemorph::ui::shell
