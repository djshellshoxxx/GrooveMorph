#include "ui/EasyView.h"

#include "PluginProcessor.h"
#include "core/easy/EasyLayout.h"
#include "core/easy/EasyText.h"

namespace groovemorph::ui
{
namespace
{
juce::Rectangle<int> toJuce(const groovemorph::core::easy::IntRect& r)
{
    return {r.x, r.y, r.w, r.h};
}

const juce::Colour backgroundColour{0xff151b24};
const juce::Colour soundingColour{0xff2e7d32};
const juce::Colour queuedColour{0xff8d6e00};
const juce::Colour idleColour{0xff3a4658};
constexpr juce::uint32 messageMillis = 9000;
} // namespace

// Larger button text than the default so the scene and action buttons read from a distance.
struct EasyView::EasyLookAndFeel final : juce::LookAndFeel_V4
{
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return juce::Font(juce::FontOptions(juce::jlimit(14.0f, 30.0f, static_cast<float>(buttonHeight) * 0.26f),
                                            juce::Font::bold));
    }
};

EasyView::EasyView(GrooveMorphAudioProcessor& p) : processor(p), lookAndFeel(std::make_unique<EasyLookAndFeel>())
{
    setOpaque(true);
    setTitle("GrooveMorph Easy mode");
    setDescription("Compact view with the four scenes, the main macros and Surprise me");

    titleLabel.setText("GrooveMorph Easy", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setTitle("GrooveMorph Easy");
    timingLabel.setFont(juce::FontOptions(17.0f));
    timingLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8fb3d9));
    timingLabel.setTitle("Timing status");
    timingLabel.setTooltip("What GrooveMorph is following for its timing, in plain words.");
    messageLabel.setFont(juce::FontOptions(16.0f));
    messageLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe3eaf2));
    messageLabel.setTitle("Message");
    for (auto* label : {&titleLabel, &timingLabel, &messageLabel})
        addAndMakeVisible(*label);

    addMacro(0, "masterIntensity", "Intensity",
             "How strongly the rhythm effects are applied. 0 gives the original sound, 1 is the full effect.");
    addMacro(1, "dryWet", "Dry / wet",
             "Mix between the original sound (0) and the processed sound (1).");
    addMacro(2, "morphAmount", "Morph",
             "Blends the sounding scene toward the next scene. 0 leaves the scene as it is.");
    addMacro(3, "chaos", "Chaos",
             "Makes the sounding scene busier: more effects, deeper and shorter slices. 0 leaves it exactly as stored. "
             "Your saved scenes are never changed.");

    for (std::size_t i = 0; i < sceneButtons.size(); ++i)
    {
        auto& button = sceneButtons[i];
        button.setLookAndFeel(lookAndFeel.get());
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        button.setTooltip("Switch to this scene. It takes over according to the Transition setting (the next bar by default).");
        button.onClick = [this, i] { onSceneClicked(i); };
        addAndMakeVisible(button);
    }

    surpriseButton.setTooltip("Tries a new variation of the sounding scene and plays it now. It is kept at the next phrase. Press again for another one.");
    surpriseButton.setTitle("Surprise me");
    surpriseButton.setDescription("Preview a new variation of the sounding scene and keep it at the next phrase");
    surpriseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6a3fb5));
    surpriseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    surpriseButton.onClick = [this] { onSurprise(); };
    undoButton.setTooltip("Takes back the pending Surprise, or otherwise the last scene change.");
    undoButton.setTitle("Undo last change");
    undoButton.setDescription("Cancel the pending Surprise, or undo the last scene edit");
    undoButton.onClick = [this] { onUndo(); };
    panicButton.setTooltip("Chaos and Morph back to 0, pending changes cancelled, evolution and gestures off. Your scenes are not changed.");
    panicButton.setTitle("Reset / Panic");
    panicButton.setDescription("Return to the plain stored scene: chaos and morph to zero, cancel pending changes");
    panicButton.onClick = [this] { onPanic(); };
    guideButton.setTooltip("Opens the four step first-run guide again.");
    guideButton.setTitle("Show guide");
    guideButton.setDescription("Open the first-run guide");
    guideButton.onClick = [this] { showGuide(); };
    fullViewButton.setTooltip("Switches to the full editor with every control and the inspector.");
    fullViewButton.setTitle("Open full view");
    fullViewButton.setDescription("Switch to the full editor with every control");
    fullViewButton.onClick = [this] { if (onOpenFullView) onOpenFullView(); };
    for (auto* button : {&surpriseButton, &undoButton, &panicButton, &guideButton, &fullViewButton})
        addAndMakeVisible(*button);
    for (auto* button : {&surpriseButton, &undoButton, &panicButton})
        button->setLookAndFeel(lookAndFeel.get());

    guide = std::make_unique<FirstRunGuide>();
    guide->onFinished = [this] { setMessage("Guide closed. Show guide brings it back."); };
    addChildComponent(*guide);
    if (!FirstRunGuide::hasBeenSeen())
        guide->open();
    setMessage({});
    refresh();
}

EasyView::~EasyView()
{
    for (auto& button : sceneButtons)
        button.setLookAndFeel(nullptr);
    for (auto* button : {&surpriseButton, &undoButton, &panicButton})
        button->setLookAndFeel(nullptr);
}

void EasyView::addMacro(std::size_t index, const char* parameter, const juce::String& label, const juce::String& help)
{
    auto& slider = sliders[index];
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 26);
    slider.setName(label);
    slider.setTitle(label);
    slider.setDescription(help);
    slider.setTooltip(help);
    slider.info() = {&processor, label, juce::String("p:") + parameter, {}};
    addAndMakeVisible(slider);
    attachments.push_back(std::make_unique<SliderAttachment>(processor.parameters, parameter, slider));
    slider.textFromValueFunction = [](double value) { return juce::String(value, 2); };
    slider.setNumDecimalPlacesToDisplay(2);

    auto& text = macroLabels[index];
    text.setText(label, juce::dontSendNotification);
    text.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    text.setColour(juce::Label::textColourId, juce::Colours::white);
    text.setTitle(label + " label");
    text.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(text);
}

void EasyView::setMessage(const juce::String& text)
{
    if (text.isEmpty())
    {
        messageLabel.setText("Tip: Surprise me plays a new variation now and keeps it at the next phrase.",
                             juce::dontSendNotification);
        messageUntil = 0;
    }
    else
    {
        messageLabel.setText(text, juce::dontSendNotification);
        messageUntil = juce::Time::getMillisecondCounter() + messageMillis;
    }
    messageLabel.setTitle(messageLabel.getText());
}

void EasyView::showGuide()
{
    if (guide != nullptr)
        guide->open();
}

void EasyView::onSceneClicked(std::size_t index)
{
    processor.cancelSurprise(); // a pending Surprise belongs to the scene that was sounding
    processor.requestVariation(index);
    setMessage("Switching to scene " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + index))
               + " using the Transition setting.");
}

void EasyView::onSurprise()
{
    using Status = GrooveMorphAudioProcessor::SurpriseStatus;
    if (processor.isSurprisePending())
        processor.cancelSurprise(); // pressing again asks for the next candidate
    const auto scene = juce::String::charToString(static_cast<juce::juce_wchar>('A' + processor.getActiveVariation()));
    switch (processor.requestSurprise())
    {
        case Status::Armed:
            setMessage("Playing a new variation of scene " + scene + ". It is kept at the next phrase. Undo last change takes it back.");
            break;
        case Status::NothingToChange:
            setMessage("Nothing to change: scene " + scene + " is locked or already as busy as it gets.");
            break;
        case Status::Busy:
            setMessage("A preview is open in the full view. Apply or revert it there first, then try again.");
            break;
    }
}

void EasyView::onUndo()
{
    if (processor.isSurprisePending())
    {
        processor.cancelSurprise();
        setMessage("Surprise cancelled. The scene is as it was.");
    }
    else if (processor.undoSceneEdit())
    {
        setMessage("Undid the last scene change.");
    }
    else
    {
        setMessage("Nothing to undo.");
    }
}

void EasyView::onPanic()
{
    processor.panicEasy();
    setMessage("Reset: Chaos and Morph are back at 0 and pending changes are cancelled. Your scenes are unchanged.");
}

void EasyView::retargetMorph()
{
    // Morph blends toward a target scene; with no target (or the sounding scene itself) it would do nothing.
    auto* amount = processor.parameters.getParameter("morphAmount");
    auto* target = processor.parameters.getParameter("morphTarget");
    if (amount == nullptr || target == nullptr)
        return;
    const double value = amount->convertFrom0to1(amount->getValue());
    const int current = static_cast<int>(std::lround(target->convertFrom0to1(target->getValue()))); // 0 = off, 1..4 = A..D
    const int sounding = static_cast<int>(std::min<std::size_t>(processor.getActiveVariation(), 3));
    if (value > 0.001 && (current == 0 || current - 1 == sounding))
    {
        const int next = (sounding + 1) % 4 + 1;
        target->beginChangeGesture();
        target->setValueNotifyingHost(target->convertTo0to1(static_cast<float>(next)));
        target->endChangeGesture();
    }
}

void EasyView::refresh()
{
    retargetMorph();

    namespace easy = groovemorph::core::easy;
    easy::EasyTimingInput timing;
    timing.prepared = processor.isPrepared();
    timing.valid = processor.hasValidTiming();
    timing.source = processor.getTimingSource();
    timing.playing = processor.isTransportPlaying();
    timing.internalClock = processor.isInternalTransportActive();
    timing.missingFields = processor.getMissingTimingFields();
    const auto timingText = juce::String(easy::describeEasyTiming(timing));
    if (timingLabel.getText() != timingText)
    {
        timingLabel.setText(timingText, juce::dontSendNotification);
        timingLabel.setTitle("Timing status: " + timingText);
    }

    const auto bank = processor.getSceneBank();
    const auto active = processor.getActiveVariation();
    const auto queued = processor.getQueuedVariation();
    for (std::size_t i = 0; i < sceneButtons.size(); ++i)
    {
        const bool sounding = i == active;
        const bool isQueued = static_cast<int>(i) == queued;
        const auto letter = juce::String::charToString(static_cast<juce::juce_wchar>('A' + i));
        const auto name = juce::String(bank.variation(i).name);
        const auto state = sounding ? juce::String("Sounding") : isQueued ? juce::String("Queued") : juce::String("Ready");
        const auto text = letter + "  " + name + "\n" + state;
        auto& button = sceneButtons[i];
        if (button.getButtonText() != text)
        {
            button.setButtonText(text);
            button.setTitle("Scene " + letter + ", " + name);
            button.setDescription(state);
        }
        button.setColour(juce::TextButton::buttonColourId, sounding ? soundingColour : isQueued ? queuedColour : idleColour);
    }

    const bool pending = processor.isSurprisePending();
    const bool previewing = processor.isAuditioning();
    surpriseButton.setEnabled(pending || !previewing);
    undoButton.setEnabled(pending || (!previewing && processor.canUndoSceneEdit()));

    const auto morph = sliders[2].getValue();
    auto* target = processor.parameters.getParameter("morphTarget");
    const int targetIndex = target != nullptr ? static_cast<int>(std::lround(target->convertFrom0to1(target->getValue()))) : 0;
    const auto morphText = morph > 0.001 && targetIndex > 0
        ? "Morph to " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + targetIndex - 1))
        : juce::String("Morph");
    if (macroLabels[2].getText() != morphText)
        macroLabels[2].setText(morphText, juce::dontSendNotification);

    if (messageUntil != 0 && juce::Time::getMillisecondCounter() > messageUntil)
        setMessage({});
}

void EasyView::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);
}

void EasyView::resized()
{
    const auto layout = groovemorph::core::easy::computeEasyLayout(getWidth(), getHeight());
    titleLabel.setBounds(toJuce(layout.title));
    guideButton.setBounds(toJuce(layout.showGuide));
    fullViewButton.setBounds(toJuce(layout.openFull));
    timingLabel.setBounds(toJuce(layout.timing));
    for (std::size_t i = 0; i < 4; ++i)
    {
        sceneButtons[i].setBounds(toJuce(layout.scenes[i]));
        macroLabels[i].setBounds(toJuce(layout.macroLabels[i]));
        sliders[i].setBounds(toJuce(layout.macroSliders[i]));
    }
    surpriseButton.setBounds(toJuce(layout.surprise));
    undoButton.setBounds(toJuce(layout.undo));
    panicButton.setBounds(toJuce(layout.panic));
    messageLabel.setBounds(toJuce(layout.message));
    if (guide != nullptr)
        guide->setBounds(getLocalBounds());
}
} // namespace groovemorph::ui
