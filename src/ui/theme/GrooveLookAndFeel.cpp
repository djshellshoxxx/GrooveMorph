#include "ui/theme/GrooveLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::ui::theme
{
namespace
{
constexpr float pi = juce::MathConstants<float>::pi;
constexpr float rotaryStart = pi * 1.25f; // 270 degree sweep, gap at the bottom
constexpr float rotaryEnd = pi * 2.75f;
constexpr float tooltipMaxWidth = 360.0f;

float textWidth(const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(f, s, 0.0f, 0.0f);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}

void drawFocusRing(juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    g.setColour(focusRing);
    g.drawRoundedRectangle(area.reduced(1.0f), corner, 2.0f);
}

juce::Path makeTick(juce::Rectangle<float> box)
{
    juce::Path p;
    p.startNewSubPath(box.getX() + box.getWidth() * 0.22f, box.getY() + box.getHeight() * 0.54f);
    p.lineTo(box.getX() + box.getWidth() * 0.43f, box.getY() + box.getHeight() * 0.74f);
    p.lineTo(box.getX() + box.getWidth() * 0.78f, box.getY() + box.getHeight() * 0.28f);
    return p;
}

juce::TextLayout layoutTip(const juce::String& tip, float maxWidth, juce::Colour colour)
{
    juce::AttributedString s;
    s.setJustification(juce::Justification::centredLeft);
    s.append(tip, font(fontLabel), colour);
    juce::TextLayout layout;
    layout.createLayout(s, maxWidth);
    return layout;
}
} // namespace

GrooveLookAndFeel::GrooveLookAndFeel()
{
    applyColours();
}

bool GrooveLookAndFeel::isHero(const juce::Component& c)
{
    return static_cast<bool>(c.getProperties().getWithDefault(heroProperty, false));
}

void GrooveLookAndFeel::applyColours()
{
    using CS = juce::LookAndFeel_V4::ColourScheme;
    // windowBackground, widgetBackground, menuBackground, outline, defaultText, defaultFill, highlightedText,
    // highlightedFill, menuText. Sets the V4 defaults for everything not listed below.
    setColourScheme(CS(bg, surfaceRaised, surfaceRaised, stroke, text, accent, textOnAccent, accent, text));

    const auto clear = juce::Colours::transparentBlack;

    setColour(juce::ResizableWindow::backgroundColourId, bg);
    setColour(juce::DocumentWindow::textColourId, text);

    setColour(juce::TextButton::buttonColourId, surfaceRaised);
    setColour(juce::TextButton::buttonOnColourId, accent);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, textOnAccent);
    setColour(juce::ToggleButton::textColourId, text);
    setColour(juce::ToggleButton::tickColourId, textOnAccent);
    setColour(juce::ToggleButton::tickDisabledColourId, textDim);
    setColour(juce::DrawableButton::textColourId, text);
    setColour(juce::DrawableButton::textColourOnId, textOnAccent);
    setColour(juce::DrawableButton::backgroundColourId, clear);
    setColour(juce::DrawableButton::backgroundOnColourId, accent);
    setColour(juce::HyperlinkButton::textColourId, accent);

    setColour(juce::TextEditor::backgroundColourId, surfaceRaised);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::highlightColourId, accentDim);
    setColour(juce::TextEditor::highlightedTextColourId, text);
    setColour(juce::TextEditor::outlineColourId, stroke);
    setColour(juce::TextEditor::focusedOutlineColourId, focusRing);
    setColour(juce::TextEditor::shadowColourId, clear);
    setColour(juce::CaretComponent::caretColourId, text);

    setColour(juce::Label::backgroundColourId, clear);
    setColour(juce::Label::textColourId, text);
    setColour(juce::Label::outlineColourId, clear);
    setColour(juce::Label::backgroundWhenEditingColourId, surfaceRaised);
    setColour(juce::Label::textWhenEditingColourId, text);
    setColour(juce::Label::outlineWhenEditingColourId, focusRing);

    setColour(juce::ScrollBar::backgroundColourId, clear);
    setColour(juce::ScrollBar::thumbColourId, textDim);
    setColour(juce::ScrollBar::trackColourId, clear);

    setColour(juce::Slider::backgroundColourId, stroke);
    setColour(juce::Slider::thumbColourId, text);
    setColour(juce::Slider::trackColourId, accent);
    setColour(juce::Slider::rotarySliderFillColourId, accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, textDim);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxBackgroundColourId, surfaceRaised);
    setColour(juce::Slider::textBoxHighlightColourId, accentDim);
    setColour(juce::Slider::textBoxOutlineColourId, stroke);

    setColour(juce::ComboBox::backgroundColourId, surfaceRaised);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::outlineColourId, stroke);
    setColour(juce::ComboBox::buttonColourId, surfaceRaised);
    setColour(juce::ComboBox::arrowColourId, text);
    setColour(juce::ComboBox::focusedOutlineColourId, focusRing);

    setColour(juce::PopupMenu::backgroundColourId, surfaceRaised);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::headerTextColourId, textDim);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour(juce::PopupMenu::highlightedTextColourId, textOnAccent);

    setColour(juce::AlertWindow::backgroundColourId, surface);
    setColour(juce::AlertWindow::textColourId, text);
    setColour(juce::AlertWindow::outlineColourId, strokeStrong);

    setColour(juce::TooltipWindow::backgroundColourId, surfaceRaised);
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, strokeStrong);

    setColour(juce::TabbedComponent::backgroundColourId, clear);
    setColour(juce::TabbedComponent::outlineColourId, stroke);
    setColour(juce::TabbedButtonBar::tabOutlineColourId, stroke);
    setColour(juce::TabbedButtonBar::tabTextColourId, textDim);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, accent);
    setColour(juce::TabbedButtonBar::frontTextColourId, text);

    setColour(juce::ListBox::backgroundColourId, surface);
    setColour(juce::ListBox::outlineColourId, stroke);
    setColour(juce::ListBox::textColourId, text);

    setColour(juce::TreeView::backgroundColourId, surface);
    setColour(juce::TreeView::linesColourId, stroke);
    setColour(juce::TreeView::dragAndDropIndicatorColourId, accent);
    setColour(juce::TreeView::selectedItemBackgroundColourId, accentDim);
    setColour(juce::TreeView::oddItemsColourId, surface);
    setColour(juce::TreeView::evenItemsColourId, surfaceRaised);

    setColour(juce::TableHeaderComponent::textColourId, text);
    setColour(juce::TableHeaderComponent::backgroundColourId, surfaceRaised);
    setColour(juce::TableHeaderComponent::outlineColourId, stroke);
    setColour(juce::TableHeaderComponent::highlightColourId, surfaceHover);

    setColour(juce::ProgressBar::backgroundColourId, surfaceRaised);
    setColour(juce::ProgressBar::foregroundColourId, accent);

    setColour(juce::GroupComponent::outlineColourId, stroke);
    setColour(juce::GroupComponent::textColourId, textDim);

    setColour(juce::BubbleComponent::backgroundColourId, surfaceRaised);
    setColour(juce::BubbleComponent::outlineColourId, strokeStrong);

    setColour(juce::PropertyComponent::backgroundColourId, surfaceRaised);
    setColour(juce::PropertyComponent::labelTextColourId, text);

    setColour(juce::ColourSelector::backgroundColourId, surface);
    setColour(juce::ColourSelector::labelTextColourId, text);

    setColour(juce::KeyMappingEditorComponent::backgroundColourId, surface);
    setColour(juce::KeyMappingEditorComponent::textColourId, text);

    setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, accentDim);
    setColour(juce::DirectoryContentsDisplayComponent::textColourId, text);
    setColour(juce::DirectoryContentsDisplayComponent::highlightedTextColourId, text);

    setColour(juce::FileBrowserComponent::currentPathBoxBackgroundColourId, surfaceRaised);
    setColour(juce::FileBrowserComponent::currentPathBoxTextColourId, text);
    setColour(juce::FileBrowserComponent::currentPathBoxArrowColourId, text);
    setColour(juce::FileBrowserComponent::filenameBoxBackgroundColourId, surfaceRaised);
    setColour(juce::FileBrowserComponent::filenameBoxTextColourId, text);
}

// ---------------------------------------------------------------------------------------------------------------
// Sliders

void GrooveLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float, float, juce::Slider& slider)
{
    const bool hero = isHero(slider);
    const float alpha = slider.isEnabled() ? 1.0f : 0.45f;
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(hero ? 6.0f : 5.0f);
    const float diameter = std::max(8.0f, std::min(bounds.getWidth(), bounds.getHeight()));
    const auto circle = bounds.withSizeKeepingCentre(diameter, diameter);
    const auto centre = circle.getCentre();
    const float thickness = hero ? 5.0f : 4.0f;
    const float arcRadius = diameter * 0.5f - thickness * 0.5f;
    const float angle = rotaryStart + std::clamp(sliderPos, 0.0f, 1.0f) * (rotaryEnd - rotaryStart);

    // Body disc.
    g.setColour(surfaceRaised.withMultipliedAlpha(alpha));
    g.fillEllipse(circle.reduced(thickness + 1.0f));

    // Track and value arcs.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStart, rotaryEnd, true);
    g.setColour(textDim.withMultipliedAlpha((hero ? 0.55f : 0.7f) * alpha));
    g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPos > 0.002f)
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStart, angle, true);
        g.setColour((hero ? accent : text).withMultipliedAlpha(alpha));
        g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Pointer dot at the end of the value arc.
    const juce::Point<float> tip(centre.x + arcRadius * std::sin(angle), centre.y - arcRadius * std::cos(angle));
    g.setColour(text.withMultipliedAlpha(alpha));
    g.fillEllipse(juce::Rectangle<float>(thickness + 2.0f, thickness + 2.0f).withCentre(tip));

    // Value text in the centre unless the slider has its own text box.
    if (slider.getTextBoxPosition() == juce::Slider::NoTextBox)
    {
        const auto inner = circle.reduced(thickness + 5.0f).toNearestInt();
        g.setColour(text.withMultipliedAlpha(alpha));
        g.setFont(font(hero ? fontValue : fontBody, hero));
        g.drawFittedText(slider.getTextFromValue(slider.getValue()), inner, juce::Justification::centred, 1, 0.7f);
    }

    if (slider.hasKeyboardFocus(true))
    {
        g.setColour(focusRing);
        g.drawEllipse(circle.reduced(1.0f), 2.0f);
    }
}

int GrooveLookAndFeel::getSliderThumbRadius(juce::Slider&)
{
    return 8;
}

void GrooveLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                         juce::Slider& slider)
{
    const bool horizontal = style == juce::Slider::LinearHorizontal;
    const bool vertical = style == juce::Slider::LinearVertical;
    if (!horizontal && !vertical)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const float alpha = slider.isEnabled() ? 1.0f : 0.45f;
    const float trackThickness = 4.0f;
    const float thumbRadius = 8.0f;
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();

    juce::Rectangle<float> trackRect, fillRect;
    juce::Point<float> thumbCentre;
    if (horizontal)
    {
        const float cy = area.getCentreY();
        trackRect = juce::Rectangle<float>(area.getX(), cy - trackThickness * 0.5f, area.getWidth(), trackThickness);
        fillRect = trackRect.withRight(std::clamp(sliderPos, trackRect.getX(), trackRect.getRight()));
        thumbCentre = {sliderPos, cy};
    }
    else
    {
        const float cx = area.getCentreX();
        trackRect = juce::Rectangle<float>(cx - trackThickness * 0.5f, area.getY(), trackThickness, area.getHeight());
        fillRect = trackRect.withTop(std::clamp(sliderPos, trackRect.getY(), trackRect.getBottom()));
        thumbCentre = {cx, sliderPos};
    }

    g.setColour(strokeStrong.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(trackRect, trackThickness * 0.5f);
    g.setColour(accent.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(fillRect, trackThickness * 0.5f);

    const auto thumb = juce::Rectangle<float>(thumbRadius * 2.0f, thumbRadius * 2.0f).withCentre(thumbCentre);
    g.setColour(text.withMultipliedAlpha(alpha));
    g.fillEllipse(thumb);
    g.setColour(bg.withMultipliedAlpha(alpha));
    g.drawEllipse(thumb, 1.0f);
    if (slider.hasKeyboardFocus(true))
    {
        g.setColour(focusRing);
        g.drawEllipse(thumb.expanded(3.0f), 2.0f);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Buttons

void GrooveLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                             bool highlighted, bool down)
{
    auto fill = backgroundColour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);
    if (down)
        fill = fill.darker(0.25f);
    else if (highlighted)
        fill = fill == surfaceRaised ? surfaceHover : fill.brighter(0.15f);

    const auto area = button.getLocalBounds().toFloat();
    g.setColour(fill);
    g.fillRoundedRectangle(area.reduced(0.5f), radius);

    const bool on = button.getToggleState();
    g.setColour(on ? accent.darker(0.3f) : stroke.brighter(0.15f));
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);

    // Toggle-style text buttons carry a small dot when on, so the state is not colour alone.
    if (on && button.getClickingTogglesState())
    {
        g.setColour(textOnAccent);
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({area.getRight() - 9.0f, area.getY() + 9.0f}));
    }

    if (button.hasKeyboardFocus(true))
        drawFocusRing(g, area, radius);
}

juce::Font GrooveLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    if (buttonHeight >= 48)
        return font(std::clamp(static_cast<float>(buttonHeight) * 0.26f, fontValue, 30.0f), true);
    return font(fontBody);
}

void GrooveLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont(getTextButtonFont(button, button.getHeight()));
    const auto colour = button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                                   : juce::TextButton::textColourOffId);
    g.setColour(colour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(space2, 2), juce::Justification::centred, 2, 0.8f);
}

void GrooveLookAndFeel::drawTickBox(juce::Graphics& g, juce::Component& component, float x, float y, float w, float h,
                                    bool ticked, bool isEnabled, bool highlighted, bool)
{
    const auto box = juce::Rectangle<float>(x, y, w, h);
    const float alpha = isEnabled ? 1.0f : 0.5f;
    g.setColour((ticked ? accent : highlighted ? surfaceHover : surfaceRaised).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(box, 4.0f);
    g.setColour((ticked ? accent.darker(0.3f) : strokeStrong).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);
    if (ticked)
    {
        g.setColour(component.findColour(isEnabled ? juce::ToggleButton::tickColourId : juce::ToggleButton::tickDisabledColourId));
        g.strokePath(makeTick(box), juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void GrooveLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    constexpr float boxSize = 18.0f;
    const float boxY = (static_cast<float>(button.getHeight()) - boxSize) * 0.5f;
    if (button.hasKeyboardFocus(true))
        drawFocusRing(g, button.getLocalBounds().toFloat(), radius);
    drawTickBox(g, button, 6.0f, boxY, boxSize, boxSize, button.getToggleState(), button.isEnabled(), highlighted, down);

    g.setColour(button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
    g.setFont(font(fontBody));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(static_cast<int>(boxSize) + 14).withTrimmedRight(4),
                     juce::Justification::centredLeft, 2, 0.85f);
}

// ---------------------------------------------------------------------------------------------------------------
// ComboBox and PopupMenu

void GrooveLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int, int, int, int,
                                     juce::ComboBox& box)
{
    const auto area = juce::Rectangle<int>(0, 0, width, height).toFloat();
    const float alpha = box.isEnabled() ? 1.0f : 0.5f;
    auto fill = box.findColour(juce::ComboBox::backgroundColourId);
    if (isButtonDown)
        fill = fill.darker(0.2f);
    else if (box.isMouseOver(true))
        fill = surfaceHover;
    g.setColour(fill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(area.reduced(0.5f), radius);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId).brighter(0.15f).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
    if (box.hasKeyboardFocus(true))
        drawFocusRing(g, area, radius);

    // Chevron.
    const auto arrowArea = juce::Rectangle<float>(static_cast<float>(width) - 24.0f, 0.0f, 24.0f, static_cast<float>(height));
    juce::Path chevron;
    const auto c = arrowArea.getCentre();
    chevron.startNewSubPath(c.x - 4.5f, c.y - 2.0f);
    chevron.lineTo(c.x, c.y + 2.5f);
    chevron.lineTo(c.x + 4.5f, c.y - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(alpha));
    g.strokePath(chevron, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font GrooveLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return font(fontBody);
}

void GrooveLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(space2, 1, std::max(0, box.getWidth() - 32), box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

juce::Font GrooveLookAndFeel::getPopupMenuFont()
{
    return font(fontValue);
}

void GrooveLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    g.setColour(strokeStrong);
    g.drawRect(0, 0, width, height, 1);
}

void GrooveLookAndFeel::getIdealPopupMenuItemSize(const juce::String& itemText, bool isSeparator, int standardMenuItemHeight,
                                                  int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight / 2 : 10;
        return;
    }
    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : 28;
    idealWidth = static_cast<int>(std::ceil(textWidth(getPopupMenuFont(), itemText))) + idealHeight * 2 + space2;
}

void GrooveLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                          bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                          const juce::String& itemText, const juce::String& shortcutKeyText,
                                          const juce::Drawable* icon, const juce::Colour* textColourToUse)
{
    if (isSeparator)
    {
        auto r = area.reduced(space2, 0);
        r.removeFromTop(r.getHeight() / 2 - 1);
        g.setColour(stroke.brighter(0.2f));
        g.fillRect(r.removeFromTop(1));
        return;
    }

    auto textColour = textColourToUse == nullptr ? findColour(juce::PopupMenu::textColourId) : *textColourToUse;
    auto r = area.reduced(1);
    if (isHighlighted && isActive)
    {
        g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRect(r);
        textColour = findColour(juce::PopupMenu::highlightedTextColourId);
    }
    g.setColour(textColour.withMultipliedAlpha(isActive ? 1.0f : 0.5f));

    r.reduce(space2, 0);
    auto f = getPopupMenuFont();
    const float maxFontHeight = static_cast<float>(r.getHeight()) / 1.3f;
    if (f.getHeight() > maxFontHeight)
        f = f.withHeight(maxFontHeight);
    g.setFont(f);

    const auto iconArea = r.removeFromLeft(static_cast<int>(std::ceil(f.getHeight() + 4.0f))).toFloat();
    if (icon != nullptr)
        icon->drawWithin(g, iconArea, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
    else if (isTicked)
        g.strokePath(makeTick(iconArea.withSizeKeepingCentre(iconArea.getWidth() * 0.8f, iconArea.getWidth() * 0.8f)),
                     juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (hasSubMenu)
    {
        const float s = f.getHeight() * 0.5f;
        const auto arrow = r.removeFromRight(static_cast<int>(s) + 4).toFloat();
        juce::Path p;
        p.startNewSubPath(arrow.getX() + 1.0f, arrow.getCentreY() - s * 0.5f);
        p.lineTo(arrow.getX() + 1.0f + s * 0.5f, arrow.getCentreY());
        p.lineTo(arrow.getX() + 1.0f, arrow.getCentreY() + s * 0.5f);
        g.strokePath(p, juce::PathStrokeType(1.6f));
    }

    r.removeFromRight(3);
    g.drawFittedText(itemText, r, juce::Justification::centredLeft, 1);
    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont(f.withHeight(std::max(fontLabel, f.getHeight() * 0.85f)));
        g.drawText(shortcutKeyText, r, juce::Justification::centredRight, true);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// TextEditor

void GrooveLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(juce::Rectangle<int>(0, 0, width, height).toFloat(), radius);
}

void GrooveLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (!editor.isEnabled())
        return;
    const auto area = juce::Rectangle<int>(0, 0, width, height).toFloat();
    if (editor.hasKeyboardFocus(true) && !editor.isReadOnly())
    {
        g.setColour(editor.findColour(juce::TextEditor::focusedOutlineColourId));
        g.drawRoundedRectangle(area.reduced(1.0f), radius, 2.0f);
    }
    else
    {
        g.setColour(editor.findColour(juce::TextEditor::outlineColourId).brighter(0.15f));
        g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Tabs: flat, selected tab has a 3 px accent underline.

void GrooveLookAndFeel::drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown)
{
    auto area = button.getActiveArea();
    const auto textArea = area;
    const bool front = button.isFrontTab();
    const auto orientation = button.getTabbedButtonBar().getOrientation();
    const float alpha = button.isEnabled() ? 1.0f : 0.5f;

    if (!front && (isMouseOver || isMouseDown))
    {
        g.setColour(surfaceHover.withAlpha(isMouseDown ? 0.9f : 0.6f));
        g.fillRect(area);
    }

    if (front)
    {
        g.setColour(accent);
        switch (orientation)
        {
            case juce::TabbedButtonBar::TabsAtTop: g.fillRect(area.removeFromBottom(3)); break;
            case juce::TabbedButtonBar::TabsAtBottom: g.fillRect(area.removeFromTop(3)); break;
            case juce::TabbedButtonBar::TabsAtLeft: g.fillRect(area.removeFromRight(3)); break;
            case juce::TabbedButtonBar::TabsAtRight: g.fillRect(area.removeFromLeft(3)); break;
        }
    }

    g.setColour((front || isMouseOver ? text : textDim).withMultipliedAlpha(alpha));
    g.setFont(font(fontValue, front));
    g.drawFittedText(button.getButtonText().trim(), textArea.reduced(space2, 2), juce::Justification::centred, 1, 0.8f);

    if (button.hasKeyboardFocus(false))
        drawFocusRing(g, textArea.toFloat(), 3.0f);
}

void GrooveLookAndFeel::drawTabAreaBehindFrontButton(juce::TabbedButtonBar& bar, juce::Graphics& g, int w, int h)
{
    g.setColour(stroke);
    switch (bar.getOrientation())
    {
        case juce::TabbedButtonBar::TabsAtTop: g.fillRect(0, h - 1, w, 1); break;
        case juce::TabbedButtonBar::TabsAtBottom: g.fillRect(0, 0, w, 1); break;
        case juce::TabbedButtonBar::TabsAtLeft: g.fillRect(w - 1, 0, 1, h); break;
        case juce::TabbedButtonBar::TabsAtRight: g.fillRect(0, 0, 1, h); break;
    }
}

int GrooveLookAndFeel::getTabButtonBestWidth(juce::TabBarButton& button, int)
{
    return static_cast<int>(std::ceil(textWidth(font(fontValue, true), button.getButtonText().trim()))) + space5 + space2;
}

int GrooveLookAndFeel::getTabButtonOverlap(int) { return 0; }
int GrooveLookAndFeel::getTabButtonSpaceAroundImage() { return 0; }

// ---------------------------------------------------------------------------------------------------------------
// ScrollBar

void GrooveLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                      bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver,
                                      bool isMouseDown)
{
    if (thumbSize <= 0)
        return;
    const auto thumb = isScrollbarVertical
        ? juce::Rectangle<int>(x + 1, thumbStartPosition, width - 2, thumbSize)
        : juce::Rectangle<int>(thumbStartPosition, y + 1, thumbSize, height - 2);
    g.setColour(textDim.withAlpha(isMouseDown ? 0.95f : isMouseOver ? 0.8f : 0.55f));
    g.fillRoundedRectangle(thumb.toFloat(), 3.0f);
}

int GrooveLookAndFeel::getDefaultScrollbarWidth() { return 8; }
bool GrooveLookAndFeel::areScrollbarButtonsVisible() { return false; }

// ---------------------------------------------------------------------------------------------------------------
// Tooltip and AlertWindow

juce::Rectangle<int> GrooveLookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                                         juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTip(tipText, tooltipMaxWidth, text);
    const int w = static_cast<int>(std::ceil(layout.getWidth())) + 20;
    const int h = static_cast<int>(std::ceil(layout.getHeight())) + 14;
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin(parentArea);
}

void GrooveLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& tip, int width, int height)
{
    const auto area = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(findColour(juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(findColour(juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);
    layoutTip(tip, static_cast<float>(width) - 20.0f, findColour(juce::TooltipWindow::textColourId))
        .draw(g, area.reduced(10.0f, 7.0f));
}

juce::Font GrooveLookAndFeel::getAlertWindowTitleFont() { return font(fontTitle, true); }
juce::Font GrooveLookAndFeel::getAlertWindowMessageFont() { return font(fontBody); }
juce::Font GrooveLookAndFeel::getAlertWindowFont() { return font(fontBody); }
} // namespace groovemorph::ui::theme
