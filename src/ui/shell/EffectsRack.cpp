#include "ui/shell/EffectsRack.h"

#include "PluginProcessor.h"
#include "ui/shell/ShellUtil.h"

#include <algorithm>

namespace groovemorph::ui::shell
{
namespace theme = groovemorph::ui::theme;

EffectsRack::EffectsRack(GrooveMorphAudioProcessor& processor)
{
    editVariation.addItemList({"Edit A", "Edit B", "Edit C", "Edit D"}, 1);
    editVariation.setSelectedId(1, juce::dontSendNotification);
    editVariation.setName("Variation to edit");
    describe(editVariation, "Variation to edit",
             "Which scene (A-D) the controls below edit. It does not change what is playing. Locked while a preview is running: apply or revert the preview first.");
    editVariation.info().processor = &processor;
    addAndMakeVisible(editVariation);

    variationName.setName("Variation name");
    describe(variationName, "Variation name", "Name of the scene being edited (shown on its button).");
    variationName.setInputRestrictions(static_cast<int>(groovemorph::core::scene::maxVariationNameLength));
    addAndMakeVisible(variationName);

    describe(copyActiveFxButton, "Copy FX",
             "Copy effect settings from the currently playing variation into the variation selected for editing. Keeps the target's name/MIDI note; one Undo restores it.");
    describe(previewButton, "Preview",
             "Audition the scene being edited without saving it: you hear your edits live, and nothing changes until you press Apply preview.");
    describe(compareButton, "Hear stored",
             "While previewing, switches between your edited version and the stored scene so you can compare them by ear.");
    describe(applyButton, "Apply preview", "Keeps the previewed edit as the scene (one Undo step).");
    describe(revertButton, "Revert preview", "Throws the previewed edit away and returns to the stored scene.");
    for (auto* b : {&copyActiveFxButton, &previewButton, &compareButton, &applyButton, &revertButton})
    {
        b->setWantsKeyboardFocus(true);
        addAndMakeVisible(*b);
    }
    editVariation.setWantsKeyboardFocus(true);
    variationName.setWantsKeyboardFocus(true);

    for (std::size_t i = 0; i < groovemorph::core::operationCount; ++i)
    {
        cards.push_back(std::make_unique<EffectCard>(processor, i));
        addAndMakeVisible(*cards.back());
    }
}

EffectsRack::~EffectsRack() = default;

bool EffectsRack::anyCardDragging() const noexcept
{
    return std::any_of(cards.begin(), cards.end(), [](const auto& card) { return card->isDragging(); });
}

void EffectsRack::paint(juce::Graphics& g)
{
    paintPanel(g, getLocalBounds());
    paintCaption(g, "Editing", editingCaption);
    paintCaption(g, "Audition", auditionCaption);
}

void EffectsRack::resized()
{
    auto area = getLocalBounds().reduced(theme::space2);
    const int gap = 6;
    auto top = area.removeFromTop(28);
    area.removeFromTop(gap);

    editingCaption = top.removeFromLeft(46);
    editVariation.setBounds(top.removeFromLeft(100));
    top.removeFromLeft(gap);
    variationName.setBounds(top.removeFromLeft(std::clamp(top.getWidth() / 5, 110, 200)));
    top.removeFromLeft(gap);
    copyActiveFxButton.setBounds(top.removeFromLeft(68));
    // Audition group sits on the right.
    const int previewWidth = 64, hearWidth = 88, applyWidth = 98, revertWidth = 102;
    const int groupWidth = 54 + previewWidth + hearWidth + applyWidth + revertWidth + 4 * gap;
    auto group = top.removeFromRight(std::min(top.getWidth(), groupWidth));
    auditionCaption = group.removeFromLeft(54);
    previewButton.setBounds(group.removeFromLeft(previewWidth));
    group.removeFromLeft(gap);
    compareButton.setBounds(group.removeFromLeft(hearWidth));
    group.removeFromLeft(gap);
    applyButton.setBounds(group.removeFromLeft(applyWidth));
    group.removeFromLeft(gap);
    revertButton.setBounds(group);

    // Seven equal cards.
    const int count = static_cast<int>(cards.size());
    const int cardGap = theme::space2;
    const int cardWidth = (area.getWidth() - (count - 1) * cardGap) / std::max(1, count);
    for (auto& card : cards)
    {
        card->setBounds(area.removeFromLeft(cardWidth));
        area.removeFromLeft(cardGap);
    }
}
} // namespace groovemorph::ui::shell
