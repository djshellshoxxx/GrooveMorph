#pragma once

#include <JuceHeader.h>

namespace groovemorph::ui::shell
{
// Big labelled scene button (A..D). Shows the scene name, the shortcut key and the state as text
// (SOUNDING / QUEUED / EDITING), never colour alone.
class ScenePad final : public juce::TextButton
{
public:
    struct State
    {
        juce::String name;
        bool sounding = false, queued = false, editing = false;
        bool operator==(const State& o) const noexcept
        {
            return name == o.name && sounding == o.sounding && queued == o.queued && editing == o.editing;
        }
    };

    ScenePad();
    void setIndex(int sceneIndex);
    void setState(const State& newState); // repaints only on change
    void tick();                          // 30 Hz: repaints a queued pad so it pulses
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String letter, keyHint;
    State state;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScenePad)
};
} // namespace groovemorph::ui::shell
