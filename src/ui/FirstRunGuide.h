#pragma once

#include <JuceHeader.h>

#include "core/easy/FirstRunSteps.h"

#include <functional>

namespace groovemorph::ui
{
// N05 first-run guide: a dismissible inline overlay with four steps, shown over the Easy view. Content and
// navigation live in core/easy/FirstRunSteps.h; this class only draws them and stores the "seen" flag in
// the per-user settings file (key easy.guideSeen). See docs/specs/N05-EASY-MODE.md section 6.
class FirstRunGuide final : public juce::Component
{
public:
    FirstRunGuide();
    // Called once the guide is dismissed or finished (the owner hides it).
    std::function<void()> onFinished;

    // Per-user flag. Both calls tolerate a missing or unwritable settings folder.
    static bool hasBeenSeen();
    static void markSeen();
    // Clears the flag (used by the snapshot tool to render the guide deterministically).
    static void resetSeen();

    // Restarts at step 1 and shows the guide (also used by the "Show guide" button).
    void open();

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    juce::Rectangle<int> cardBounds() const;
    void update();
    void finish();

    groovemorph::core::easy::FirstRunProgress progress;
    juce::Label stepLabel, titleLabel, bodyLabel;
    juce::TextButton backButton{"Back"}, nextButton{"Next"}, skipButton{"Skip guide"};
};
} // namespace groovemorph::ui
