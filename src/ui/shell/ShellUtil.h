#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "ui/ControlMenu.h"
#include "ui/theme/Theme.h"

#include <memory>
#include <vector>

// Small helpers shared by the shell components (docs/specs/UI-REDESIGN.md section 4).
namespace groovemorph::ui::shell
{
using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

// Tooltip, accessible title and description in one call (UI-AX-1: tooltip >= 12 characters and a title).
inline void describe(juce::Component& component, const juce::String& title, const juce::String& tip)
{
    if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(&component))
        client->setTooltip(tip);
    component.setTitle(title);
    component.setDescription(tip);
}

// Horizontal slider with a value box (the look the old editor used for every slider).
inline void configureLinear(juce::Slider& slider, const juce::String& name, int boxWidth = 66, int boxHeight = 24)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, boxWidth, boxHeight);
    slider.setName(name);
    slider.setTooltip(name);
}

// Rotary knob: value text and caption are drawn by GrooveLookAndFeel (caption = Component name).
inline void configureKnob(juce::Slider& slider, const juce::String& caption)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setMouseDragSensitivity(200);
    slider.setName(caption);
    slider.setTooltip(caption);
}

// Slider bound to a host parameter, with the MIDI-link menu target "p:<id>".
inline void bindSlider(GrooveMorphAudioProcessor& processor, juce::Component& parent, groovemorph::ui::MenuSlider& slider,
                       std::vector<std::unique_ptr<SliderAttachment>>& attachments, const juce::String& parameter,
                       const juce::String& menuName)
{
    slider.info() = {&processor, menuName, "p:" + parameter, {}};
    parent.addAndMakeVisible(slider);
    attachments.push_back(std::make_unique<SliderAttachment>(processor.parameters, parameter, slider));
}

inline void bindCombo(GrooveMorphAudioProcessor& processor, juce::Component& parent, groovemorph::ui::MenuComboBox& combo,
                      std::vector<std::unique_ptr<ComboAttachment>>& attachments, const juce::String& parameter,
                      const juce::StringArray& choices, const juce::String& name)
{
    combo.addItemList(choices, 1);
    combo.setName(name);
    combo.setTooltip(name);
    combo.info() = {&processor, name, "p:" + parameter, {}};
    parent.addAndMakeVisible(combo);
    attachments.push_back(std::make_unique<ComboAttachment>(processor.parameters, parameter, combo));
}

// Rounded panel background used by every band.
inline void paintPanel(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto r = bounds.toFloat().reduced(0.5f);
    g.setColour(theme::surface);
    g.fillRoundedRectangle(r, theme::radiusLarge);
    g.setColour(theme::stroke);
    g.drawRoundedRectangle(r, theme::radiusLarge, 1.0f);
}

inline void paintCaption(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                         juce::Justification just = juce::Justification::centredLeft)
{
    g.setColour(theme::textDim);
    g.setFont(theme::font(theme::fontLabel));
    g.drawText(text, area, just, true);
}
} // namespace groovemorph::ui::shell
