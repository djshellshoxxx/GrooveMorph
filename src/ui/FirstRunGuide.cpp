#include "ui/FirstRunGuide.h"

namespace groovemorph::ui
{
namespace
{
constexpr auto seenKey = "easy.guideSeen";

std::unique_ptr<juce::PropertiesFile> openSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "GrooveMorph";
    options.filenameSuffix = ".easy.settings"; // separate file from the Visual window's, so neither overwrites the other
    options.folderName = "GrooveMorph";
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return std::make_unique<juce::PropertiesFile>(options);
}
} // namespace

bool FirstRunGuide::hasBeenSeen()
{
    if (auto settings = openSettings())
        return settings->getBoolValue(seenKey, false);
    return false;
}

void FirstRunGuide::markSeen()
{
    if (auto settings = openSettings())
    {
        settings->setValue(seenKey, true);
        settings->saveIfNeeded();
    }
}

FirstRunGuide::FirstRunGuide()
{
    setTitle("First-run guide");
    setDescription("A four step introduction to Easy mode. Use Next and Back, or Skip guide to close it.");
    setWantsKeyboardFocus(true);
    setFocusContainerType(juce::Component::FocusContainerType::focusContainer);

    stepLabel.setJustificationType(juce::Justification::centredLeft);
    stepLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8fb3d9));
    stepLabel.setTitle("Guide progress");
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setTitle("Guide step title");
    bodyLabel.setFont(juce::FontOptions(17.0f));
    bodyLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe3eaf2));
    bodyLabel.setJustificationType(juce::Justification::topLeft);
    bodyLabel.setMinimumHorizontalScale(1.0f);
    bodyLabel.setTitle("Guide step text");
    for (auto* label : {&stepLabel, &titleLabel, &bodyLabel})
        addAndMakeVisible(*label);

    backButton.setTitle("Back");
    backButton.setDescription("Go to the previous guide step");
    backButton.setTooltip("Go back to the previous step of the guide.");
    backButton.onClick = [this] { progress.back(); update(); };
    nextButton.setTitle("Next");
    nextButton.setDescription("Go to the next guide step, or finish on the last step");
    nextButton.setTooltip("Go to the next step. On the last step this finishes the guide.");
    nextButton.onClick = [this] {
        progress.next();
        if (progress.finished()) finish(); else update();
    };
    skipButton.setTitle("Skip guide");
    skipButton.setDescription("Close the guide. It will not open by itself again");
    skipButton.setTooltip("Close the guide. It will not open by itself again; Show guide brings it back.");
    skipButton.onClick = [this] { progress.dismiss(); finish(); };
    for (auto* button : {&backButton, &nextButton, &skipButton})
        addAndMakeVisible(*button);
    update();
}

void FirstRunGuide::open()
{
    progress.restart();
    update();
    setVisible(true);
    toFront(false);
    grabKeyboardFocus();
}

void FirstRunGuide::finish()
{
    markSeen();
    setVisible(false);
    if (onFinished)
        onFinished();
}

void FirstRunGuide::update()
{
    const auto& step = groovemorph::core::easy::firstRunStep(progress.index());
    stepLabel.setText("Step " + juce::String(static_cast<int>(progress.index()) + 1) + " of "
                          + juce::String(static_cast<int>(groovemorph::core::easy::firstRunStepCount)),
                      juce::dontSendNotification);
    titleLabel.setText(juce::String::fromUTF8(step.title), juce::dontSendNotification);
    bodyLabel.setText(juce::String::fromUTF8(step.body), juce::dontSendNotification);
    backButton.setEnabled(!progress.isFirst());
    nextButton.setButtonText(progress.isLast() ? "Done" : "Next");
    nextButton.setTitle(progress.isLast() ? "Done" : "Next");
    setDescription("First-run guide, " + stepLabel.getText() + ": " + titleLabel.getText());
}

juce::Rectangle<int> FirstRunGuide::cardBounds() const
{
    const int w = juce::jmin(560, getWidth() - 40);
    const int h = juce::jmin(300, getHeight() - 40);
    return juce::Rectangle<int>(w, h).withCentre(getLocalBounds().getCentre());
}

void FirstRunGuide::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.6f));
    const auto card = cardBounds().toFloat();
    g.setColour(juce::Colour(0xff1f2a3a));
    g.fillRoundedRectangle(card, 10.0f);
    g.setColour(juce::Colour(0xff8fb3d9));
    g.drawRoundedRectangle(card.reduced(1.0f), 10.0f, 2.0f);
}

void FirstRunGuide::resized()
{
    auto area = cardBounds().reduced(24, 20);
    stepLabel.setBounds(area.removeFromTop(24));
    titleLabel.setBounds(area.removeFromTop(40));
    auto buttons = area.removeFromBottom(44);
    bodyLabel.setBounds(area.reduced(0, 6));
    skipButton.setBounds(buttons.removeFromLeft(130));
    nextButton.setBounds(buttons.removeFromRight(110));
    buttons.removeFromRight(8);
    backButton.setBounds(buttons.removeFromRight(110));
}

bool FirstRunGuide::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        progress.dismiss();
        finish();
        return true;
    }
    return false;
}
} // namespace groovemorph::ui
