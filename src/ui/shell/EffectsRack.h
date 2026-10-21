#pragma once

#include <JuceHeader.h>

#include "ui/ControlMenu.h"
#include "ui/shell/EffectCard.h"

#include <memory>
#include <vector>

class GrooveMorphAudioProcessor;

namespace groovemorph::ui::shell
{
// Scene editing row (edit-scene combo, name, Copy FX, audition buttons) above seven effect cards.
// The owner wires all callbacks; the cards use the same edit path the old operation rows used.
class EffectsRack final : public juce::Component
{
public:
    static constexpr int height = 92;

    explicit EffectsRack(GrooveMorphAudioProcessor& processor);
    ~EffectsRack() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    groovemorph::ui::MenuComboBox editVariation;
    juce::TextEditor variationName;
    juce::TextButton copyActiveFxButton{"Copy FX"};
    juce::TextButton previewButton{"Preview"}, compareButton{"Hear stored"}, applyButton{"Apply preview"},
                     revertButton{"Revert preview"};
    std::vector<std::unique_ptr<EffectCard>> cards;

    [[nodiscard]] bool anyCardDragging() const noexcept;

private:
    juce::Rectangle<int> editingCaption, auditionCaption;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsRack)
};
} // namespace groovemorph::ui::shell
