#pragma once

#include <JuceHeader.h>

#include "ui/ControlMenu.h"

#include <cstddef>
#include <functional>

class GrooveMorphAudioProcessor;

namespace groovemorph::ui::shell
{
// One effect of the scene being edited: colour bar, glyph, name with the Enable toggle, Depth and Slice.
// This is the old OperationRow with the same tooltips, double-click returns and "s:<effect>:<field>" MIDI
// targets. `onEdit(effect, field)` is called with field 0 enable, 1 depth, 2 slice; not while a drag is running
// (it is called once at drag end) and never from setOperation().
class EffectCard final : public juce::Component
{
public:
    EffectCard(GrooveMorphAudioProcessor& processor, std::size_t effectIndex);
    ~EffectCard() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // Writes values into the controls without notifying.
    void setOperation(bool enabled, float depth, float sliceBeats);
    [[nodiscard]] bool isDragging() const noexcept { return draggingDepth || draggingSlice; }

    std::function<void(std::size_t, int)> onEdit;
    groovemorph::ui::MenuToggleButton enabled;
    groovemorph::ui::MenuSlider depth, slice;

private:
    std::size_t index;
    bool draggingDepth = false, draggingSlice = false;
    juce::Rectangle<int> glyphArea, depthCaption, sliceCaption;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectCard)
};
} // namespace groovemorph::ui::shell
