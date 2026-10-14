#include "ui/ControlMenu.h"

#include "PluginProcessor.h"

#include <cmath>

namespace groovemorph::ui
{
ClipboardAccess& ClipboardAccess::instance()
{
    static ClipboardAccess access{
        [](const juce::String& t) { juce::SystemClipboard::copyTextToClipboard(t); },
        [] { return juce::SystemClipboard::getTextFromClipboard(); }};
    return access;
}

bool parseNumber(const juce::String& rawText, double& value)
{
    auto text = rawText.trim().replaceCharacter(',', '.');
    int start = -1;
    for (int i = 0; i < text.length(); ++i)
    {
        const auto c = text[i];
        if ((c >= '0' && c <= '9') || ((c == '-' || c == '+' || c == '.') && i + 1 < text.length()
                                        && ((text[i + 1] >= '0' && text[i + 1] <= '9') || text[i + 1] == '.')))
        {
            start = i;
            break;
        }
    }
    if (start < 0)
        return false;
    int end = start + 1;
    bool seenDot = text[start] == '.';
    while (end < text.length())
    {
        const auto c = text[end];
        if (c >= '0' && c <= '9')
            ++end;
        else if (c == '.' && !seenDot)
        {
            seenDot = true;
            ++end;
        }
        else
            break;
    }
    const auto number = text.substring(start, end);
    if (!number.containsAnyOf("0123456789"))
        return false;
    const double parsed = number.getDoubleValue();
    if (!std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}

juce::String formatNumber(double value)
{
    auto s = juce::String(value, 6);
    if (s.contains("."))
    {
        s = s.trimCharactersAtEnd("0");
        s = s.trimCharactersAtEnd(".");
    }
    return s == "-0" ? juce::String("0") : s;
}

bool applyTextToSlider(juce::Slider& slider, const juce::String& text)
{
    double v = 0.0;
    if (!parseNumber(text, v))
        return false;
    const auto range = slider.getRange();
    v = juce::jlimit(range.getStart(), range.getEnd(), v);
    slider.setValue(v, juce::sendNotificationSync);
    if (slider.onDragEnd) // scene sliders commit one undo step on drag end
        slider.onDragEnd();
    return true;
}

bool applyTextToComboBox(juce::ComboBox& combo, const juce::String& text)
{
    const auto t = text.trim();
    if (t.isEmpty())
        return false;
    for (int i = 0; i < combo.getNumItems(); ++i)
        if (combo.getItemText(i).equalsIgnoreCase(t))
        {
            combo.setSelectedItemIndex(i, juce::sendNotificationSync);
            return true;
        }
    double number = 0.0;
    if (t.containsOnly("0123456789") && parseNumber(t, number))
    {
        const int index = static_cast<int>(number) - 1;
        if (index >= 0 && index < combo.getNumItems())
        {
            combo.setSelectedItemIndex(index, juce::sendNotificationSync);
            return true;
        }
    }
    return false;
}

bool applyTextToToggle(juce::ToggleButton& toggle, const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    bool on = false;
    if (t == "on" || t == "true" || t == "1" || t == "yes")
        on = true;
    else if (t == "off" || t == "false" || t == "0" || t == "no")
        on = false;
    else
        return false;
    toggle.setToggleState(on, juce::sendNotificationSync);
    return true;
}

// Repaints a control when its MIDI link or learn state changes (cheap: 4 Hz, compares two values).
struct MenuSupport::Poller final : private juce::Timer
{
    Poller(MenuSupport& s, juce::Component& c) : support(s), component(c) { startTimerHz(4); }
    ~Poller() override { stopTimer(); }
    void timerCallback() override
    {
        if (support.menu.processor == nullptr || support.menu.midiTarget.isEmpty())
            return;
        const int cc = support.menu.processor->getMidiLinkCc(support.menu.midiTarget);
        const bool learning = support.menu.processor->getMidiLearnTarget() == support.menu.midiTarget;
        if (cc != lastCc || learning != lastLearning)
        {
            lastCc = cc;
            lastLearning = learning;
            component.repaint();
        }
    }
    MenuSupport& support;
    juce::Component& component;
    int lastCc = -2;
    bool lastLearning = false;
};

MenuSupport::MenuSupport(juce::Component& o) : owner(o), poller(std::make_unique<Poller>(*this, o)) {}
MenuSupport::~MenuSupport() = default;

void MenuSupport::paintMidiOverlay(juce::Graphics& g)
{
    if (menu.processor == nullptr || menu.midiTarget.isEmpty())
        return;
    const bool learning = menu.processor->getMidiLearnTarget() == menu.midiTarget;
    const int cc = menu.processor->getMidiLinkCc(menu.midiTarget);
    if (!learning && cc < 0)
        return;
    const auto accent = juce::Colour(0xffe69f00);
    const auto b = owner.getLocalBounds().toFloat();
    if (learning)
    {
        g.setColour(accent);
        g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 2.0f);
    }
    const juce::String text = learning ? "move a MIDI controller..." : "CC " + juce::String(cc);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    const int w = juce::jmin(owner.getWidth(), static_cast<int>(text.length()) * 6 + 8);
    const auto tag = juce::Rectangle<int>(owner.getWidth() - w, 0, w, 12);
    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.fillRect(tag);
    g.setColour(accent);
    g.drawText(text, tag, juce::Justification::centred);
}

void finishControlMenus(juce::Component& root, GrooveMorphAudioProcessor& processor)
{
    auto nameFrom = [](const juce::String& tooltip, const juce::String& fallback) {
        auto t = tooltip.upToFirstOccurrenceOf(".", false, false).trim();
        if (t.isEmpty())
            return fallback;
        return t.length() > 48 ? t.substring(0, 45) + "..." : t;
    };
    for (auto* child : root.getChildren())
    {
        if (auto* s = dynamic_cast<MenuSlider*>(child))
        {
            s->info().processor = &processor;
            if (s->info().name.isEmpty())
                s->info().name = nameFrom(s->getTooltip(), s->getName());
        }
        else if (auto* c = dynamic_cast<MenuComboBox*>(child))
        {
            c->info().processor = &processor;
            if (c->info().name.isEmpty())
                c->info().name = nameFrom(c->getTooltip(), c->getName());
        }
        else if (auto* b = dynamic_cast<MenuToggleButton*>(child))
        {
            b->info().processor = &processor;
            if (b->info().name.isEmpty())
                b->info().name = b->getButtonText().isNotEmpty() ? b->getButtonText() : nameFrom(b->getTooltip(), b->getName());
        }
        finishControlMenus(*child, processor);
    }
}

void MenuSupport::resetToDefault()
{
    if (menu.processor != nullptr && menu.midiTarget.startsWith("p:"))
    {
        if (auto* parameter = menu.processor->parameters.getParameter(menu.midiTarget.substring(2)))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->getDefaultValue());
            parameter->endChangeGesture();
            return;
        }
    }
    if (resetFallback)
        resetFallback();
}

bool MenuSupport::showMenu(const juce::MouseEvent& e)
{
    if (!e.mods.isPopupMenu())
        return false;
    juce::PopupMenu m;
    m.addSectionHeader(menu.name.isNotEmpty() ? menu.name : juce::String("Control"));
    if (enterExact)
        m.addItem(1, "Enter exact value...");
    m.addItem(2, "Copy value", static_cast<bool>(copyText));
    bool canPaste = false;
    if (pasteText)
    {
        double ignored = 0.0;
        const auto clip = ClipboardAccess::instance().paste ? ClipboardAccess::instance().paste() : juce::String();
        canPaste = clip.trim().isNotEmpty() && (parseNumber(clip, ignored) || clip.trim().length() < 40);
    }
    m.addItem(3, "Paste value", canPaste);
    m.addItem(4, "Reset to default", menu.midiTarget.startsWith("p:") || (resetFallback != nullptr && (!resetAvailable || resetAvailable())));
    m.addSeparator();
    const bool linkable = menu.processor != nullptr && menu.midiTarget.isNotEmpty();
    const int cc = linkable ? menu.processor->getMidiLinkCc(menu.midiTarget) : -1;
    const bool learning = linkable && menu.processor->getMidiLearnTarget() == menu.midiTarget;
    if (learning)
        m.addItem(5, "Cancel MIDI learn");
    else
        m.addItem(5, linkable ? (cc >= 0 ? "Re-learn MIDI controller (now CC " + juce::String(cc) + ")..." : juce::String("Link to MIDI controller (learn)..."))
                              : juce::String("Link to MIDI (not available for this control)"),
                  linkable);
    if (cc >= 0)
        m.addItem(6, "Unlink MIDI (CC " + juce::String(cc) + ")");

    juce::Component::SafePointer<juce::Component> safe(&owner);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&owner).withMinimumWidth(240),
                    [this, safe](int result) {
                        if (safe == nullptr || result == 0)
                            return;
                        switch (result)
                        {
                            case 1: if (enterExact) enterExact(); break;
                            case 2: if (copyText) ClipboardAccess::instance().copy(copyText()); break;
                            case 3: if (pasteText) pasteText(ClipboardAccess::instance().paste()); break;
                            case 4: resetToDefault(); break;
                            case 5:
                                if (menu.processor != nullptr)
                                {
                                    if (menu.processor->getMidiLearnTarget() == menu.midiTarget)
                                        menu.processor->cancelMidiLearn();
                                    else
                                        menu.processor->beginMidiLearn(menu.midiTarget);
                                    owner.repaint();
                                }
                                break;
                            case 6:
                                if (menu.processor != nullptr)
                                {
                                    menu.processor->unlinkMidi(menu.midiTarget);
                                    owner.repaint();
                                }
                                break;
                            default: break;
                        }
                    });
    return true;
}

namespace
{
void askForValue(juce::Component& owner, const juce::String& title, const juce::String& current, const juce::String& hint,
                 std::function<bool(const juce::String&)> apply)
{
    auto* window = new juce::AlertWindow(title, hint, juce::MessageBoxIconType::NoIcon, &owner);
    window->addTextEditor("value", current, "Value");
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> safe(window);
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([safe, apply](int result) {
                                if (result == 1 && safe != nullptr)
                                {
                                    if (!apply(safe->getTextEditorContents("value")))
                                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Value not understood",
                                                                                "Please type a number.");
                                }
                            }),
                            true);
}
} // namespace

MenuSlider::MenuSlider() : support(*this)
{
    support.copyText = [this] { return formatNumber(getValue()); };
    support.pasteText = [this](const juce::String& t) { return applyTextToSlider(*this, t); };
    support.enterExact = [this] {
        const auto range = getRange();
        askForValue(*this, support.menu.name.isNotEmpty() ? support.menu.name : juce::String("Enter value"), formatNumber(getValue()),
                    "Type the exact value (" + formatNumber(range.getStart()) + " to " + formatNumber(range.getEnd())
                        + (support.menu.unit.isNotEmpty() ? " " + support.menu.unit : juce::String()) + ").",
                    [this](const juce::String& t) { return applyTextToSlider(*this, t); });
    };
    support.resetAvailable = [this] { return isDoubleClickReturnEnabled(); };
    support.resetFallback = [this] {
        if (isDoubleClickReturnEnabled())
        {
            setValue(getDoubleClickReturnValue(), juce::sendNotificationSync);
            if (onDragEnd)
                onDragEnd();
        }
    };
}

void MenuSlider::mouseDown(const juce::MouseEvent& e)
{
    swallowing = support.showMenu(e);
    if (!swallowing)
        juce::Slider::mouseDown(e);
}
void MenuSlider::mouseDrag(const juce::MouseEvent& e)
{
    if (!swallowing)
        juce::Slider::mouseDrag(e);
}
void MenuSlider::mouseUp(const juce::MouseEvent& e)
{
    if (!swallowing)
        juce::Slider::mouseUp(e);
    swallowing = false;
}

MenuComboBox::MenuComboBox() : support(*this)
{
    support.copyText = [this] { return getText(); };
    support.pasteText = [this](const juce::String& t) { return applyTextToComboBox(*this, t); };
}
void MenuComboBox::mouseDown(const juce::MouseEvent& e)
{
    swallowing = support.showMenu(e);
    if (!swallowing)
        juce::ComboBox::mouseDown(e);
}
void MenuComboBox::mouseUp(const juce::MouseEvent& e)
{
    if (!swallowing)
        juce::ComboBox::mouseUp(e);
    swallowing = false;
}

MenuToggleButton::MenuToggleButton(const juce::String& text) : juce::ToggleButton(text), support(*this)
{
    support.copyText = [this] { return juce::String(getToggleState() ? "on" : "off"); };
    support.pasteText = [this](const juce::String& t) { return applyTextToToggle(*this, t); };
}
void MenuToggleButton::mouseDown(const juce::MouseEvent& e)
{
    swallowing = support.showMenu(e);
    if (!swallowing)
        juce::ToggleButton::mouseDown(e);
}
void MenuToggleButton::mouseUp(const juce::MouseEvent& e)
{
    if (!swallowing)
        juce::ToggleButton::mouseUp(e);
    swallowing = false;
}
} // namespace groovemorph::ui
