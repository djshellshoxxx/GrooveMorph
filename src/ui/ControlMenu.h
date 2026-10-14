#pragma once

#include <JuceHeader.h>

#include <functional>

class GrooveMorphAudioProcessor;

namespace groovemorph::ui
{
// Right-click menu for controls: enter an exact value, copy / paste the value, reset to default, link to a
// MIDI controller (learn) or unlink. Drop-in replacements for Slider / ComboBox / ToggleButton.

// What a control knows about itself for the menu.
struct ControlMenuInfo
{
    GrooveMorphAudioProcessor* processor = nullptr;
    juce::String name;       // shown in dialogs
    juce::String midiTarget; // "p:<parameterId>", "s:<effect>:<field>", or empty = cannot be linked to MIDI
    juce::String unit;       // e.g. "BPM", appended in the dialog text
};

// Clipboard access is replaceable so tests need no display.
struct ClipboardAccess
{
    std::function<void(const juce::String&)> copy;
    std::function<juce::String()> paste;
    static ClipboardAccess& instance();
};

// Pure helpers.
// Parses a number from user text: optional sign, digits, optional decimal part, optional trailing unit/words.
// Accepts ',' as the decimal separator. False when no number is found.
[[nodiscard]] bool parseNumber(const juce::String& text, double& value);
// Shortest decimal text that round-trips for display and copy (no trailing zeros), at most 6 decimals.
[[nodiscard]] juce::String formatNumber(double value);

class MenuSlider;
class MenuComboBox;
class MenuToggleButton;

// Gives every menu control below `root` the processor and, when it has no name yet, one taken from its
// tooltip or text. Call once after building a panel.
void finishControlMenus(juce::Component& root, GrooveMorphAudioProcessor& processor);

// Applies typed or pasted text to a control; false (and no change) when it cannot be understood.
// Sliders: clamped to the slider's range. Combo boxes: item text (case-insensitive) or 1-based number.
// Toggles: on/off/true/false/1/0.
bool applyTextToSlider(juce::Slider& slider, const juce::String& text);
bool applyTextToComboBox(juce::ComboBox& combo, const juce::String& text);
bool applyTextToToggle(juce::ToggleButton& toggle, const juce::String& text);

// Shared mixin: MIDI badge / learn outline painting and the menu.
class MenuSupport
{
public:
    explicit MenuSupport(juce::Component& owner);
    ~MenuSupport();
    ControlMenuInfo menu;
    void paintMidiOverlay(juce::Graphics&);
    [[nodiscard]] bool showMenu(const juce::MouseEvent& e); // true when the event was a menu request and was handled
    void resetToDefault();

    std::function<juce::String()> copyText;                 // current value as text
    std::function<bool(const juce::String&)> pasteText;     // false = not understood
    std::function<void()> enterExact;                       // nullptr = not offered
    std::function<void()> resetFallback;                    // used when there is no parameter to reset
    std::function<bool()> resetAvailable;                   // whether resetFallback can do anything

private:
    juce::Component& owner;
    struct Poller;
    std::unique_ptr<Poller> poller;
};

class MenuSlider : public juce::Slider
{
public:
    MenuSlider();
    ControlMenuInfo& info() { return support.menu; }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void paintOverChildren(juce::Graphics& g) override { support.paintMidiOverlay(g); }

private:
    MenuSupport support;
    bool swallowing = false;
};

class MenuComboBox : public juce::ComboBox
{
public:
    MenuComboBox();
    ControlMenuInfo& info() { return support.menu; }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void paintOverChildren(juce::Graphics& g) override { support.paintMidiOverlay(g); }

private:
    MenuSupport support;
    bool swallowing = false;
};

class MenuToggleButton : public juce::ToggleButton
{
public:
    explicit MenuToggleButton(const juce::String& text = {});
    ControlMenuInfo& info() { return support.menu; }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void paintOverChildren(juce::Graphics& g) override { support.paintMidiOverlay(g); }

private:
    MenuSupport support;
    bool swallowing = false;
};
} // namespace groovemorph::ui
