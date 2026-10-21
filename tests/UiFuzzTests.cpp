// Randomized UI exerciser: builds the real editor and drives every visible control (buttons, sliders, combo
// boxes, text editors, keys, tabs, resizes) in random order while the processor renders audio, then checks the
// processor stays finite and the editor survives. Needs a display (Xvfb in CI). Controls that open native
// file choosers or blocking popup menus are skipped by label.
// Usage: GrooveMorphUiFuzzTests [rounds=20] [firstSeed=1] [actionsPerRound=400]
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>

namespace
{
int failures = 0;
std::map<std::string, int> coverage;

class FakePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

void pump(int ms)
{
    juce::Timer::callAfterDelay(ms, [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
}

bool skipLabel(const juce::String& t)
{
    for (const char* bad : {"Import", "Browse", "Save", "Export", "Load", "Open", "Folder", "Reveal", "...", "Delete"})
        if (t.containsIgnoreCase(bad))
            return true;
    return false;
}

bool chainVisible(juce::Component& c)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (!p->isVisible())
            return false;
    return true;
}

void collect(juce::Component& c, std::vector<juce::Component*>& out)
{
    for (auto* child : c.getChildren())
    {
        if (!child->isVisible())
            continue;
        if (dynamic_cast<juce::Button*>(child) || dynamic_cast<juce::Slider*>(child) || dynamic_cast<juce::ComboBox*>(child)
            || dynamic_cast<juce::TextEditor*>(child))
            out.push_back(child);
        collect(*child, out);
    }
}

int usage(juce::Component& c)
{
    if (auto* b = dynamic_cast<juce::Button*>(&c)) return coverage["button:" + b->getButtonText().toStdString()];
    if (auto* s = dynamic_cast<juce::Slider*>(&c)) return coverage["slider:" + s->getName().toStdString()];
    if (auto* cb = dynamic_cast<juce::ComboBox*>(&c)) return coverage["combo:" + cb->getName().toStdString()];
    return coverage["text:" + c.getName().toStdString()];
}

void act(juce::Component& target, juce::Random& r)
{
    const std::string kind = typeid(target).name();
    if (auto* b = dynamic_cast<juce::Button*>(&target))
    {
        if (skipLabel(b->getButtonText()) || skipLabel(b->getTooltip()) || !b->isEnabled())
            return;
        ++coverage["button:" + b->getButtonText().toStdString()];
        if (b->getClickingTogglesState())
            b->setToggleState(!b->getToggleState(), juce::sendNotificationSync);
        else if (b->onClick)
            b->onClick();
    }
    else if (auto* s = dynamic_cast<juce::Slider*>(&target))
    {
        if (!s->isEnabled())
            return;
        ++coverage["slider:" + s->getName().toStdString()];
        const double lo = s->getMinimum(), hi = s->getMaximum();
        const int mode = r.nextInt(6);
        const double v = mode == 0 ? lo : mode == 1 ? hi : lo + (hi - lo) * r.nextDouble();
        s->setValue(v, juce::sendNotificationSync);
    }
    else if (auto* cb = dynamic_cast<juce::ComboBox*>(&target))
    {
        if (!cb->isEnabled() || cb->getNumItems() == 0)
            return;
        ++coverage["combo:" + cb->getName().toStdString()];
        cb->setSelectedItemIndex(r.nextInt(cb->getNumItems()), juce::sendNotificationSync);
    }
    else if (auto* te = dynamic_cast<juce::TextEditor*>(&target))
    {
        if (te->isReadOnly() || !te->isEnabled())
            return;
        ++coverage["text:" + te->getName().toStdString()];
        static const char* texts[] = {"", "0", "-1", "999999999999", "abc", "1.5e308", "NaN", "inf", "\xE2\x98\x83 snowman",
                                      "a very long name that goes on and on and on and on and on and on and on and on"};
        te->setText(juce::String::fromUTF8(texts[r.nextInt(10)]), true);
        te->onReturnKey ? te->onReturnKey() : void();
        te->onFocusLost ? te->onFocusLost() : void();
    }
}

void renderBlocks(GrooveMorphAudioProcessor& p, FakePlayHead& ph, juce::Random& r, std::int64_t& pos, int blocks, int& bad)
{
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < blocks; ++i)
    {
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 512; ++k)
                buf.setSample(c, k, r.nextFloat() * 1.2f - 0.6f);
        const double ppq = (double) pos / 48000.0 * 2.0;
        ph.info = {};
        ph.info.setBpm(120.0);
        ph.info.setTimeSignature(juce::AudioPlayHead::TimeSignature{4, 4});
        ph.info.setIsPlaying(true);
        ph.info.setTimeInSamples(pos);
        ph.info.setPpqPosition(ppq);
        ph.info.setPpqPositionOfLastBarStart(std::floor(ppq / 4.0) * 4.0);
        midi.clear();
        p.processBlock(buf, midi);
        pos += 512;
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 512; ++k)
                if (!std::isfinite(buf.getSample(c, k)))
                    ++bad;
    }
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const int rounds = argc > 1 ? std::atoi(argv[1]) : 20;
    const std::uint64_t first = argc > 2 ? (std::uint64_t) std::atoll(argv[2]) : 1;
    const int actions = argc > 3 ? std::atoi(argv[3]) : 400;

    for (int round = 0; round < rounds; ++round)
    {
        const std::uint64_t seed = first + (std::uint64_t) round;
        juce::Random r((juce::int64) (seed * 0x9e3779b97f4a7c15ull + 77));
        for (int w = 0; w < 8; ++w) (void) r.nextInt();
        GrooveMorphAudioProcessor processor;
        FakePlayHead ph;
        processor.setPlayHead(&ph);
        processor.prepareToPlay(48000.0, 512);
        processor.setSourceMode(r.nextInt(2));
        processor.setEasyMode(r.nextBool());
        std::int64_t pos = 0;
        int bad = 0;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setVisible(true);
        editor->setSize(900 + r.nextInt(800), 600 + r.nextInt(500));
        auto* gm = dynamic_cast<GrooveMorphAudioProcessorEditor*>(editor.get());
        int done = 0;
        for (int a = 0; a < actions; ++a)
        {
            std::cerr << "ui seed=" << seed << " action=" << a << '\r' << std::flush;
            std::vector<juce::Component*> controls;
            collect(*editor, controls);
            if (!controls.empty())
            {
                // Favour controls driven least so far, otherwise the always-visible sliders soak up every action.
                juce::Component* pick = controls[(std::size_t) r.nextInt((int) controls.size())];
                for (int t = 0; t < 4; ++t)
                {
                    auto* other = controls[(std::size_t) r.nextInt((int) controls.size())];
                    if (usage(*other) < usage(*pick))
                        pick = other;
                }
                act(*pick, r);
            }
            switch (r.nextInt(14))
            {
                case 0: if (gm) gm->showInspectorTab(r.nextInt(9)); break;
                case 1: editor->setSize(700 + r.nextInt(1300), 500 + r.nextInt(800)); break;
                case 2: editor->keyPressed(juce::KeyPress(r.nextInt(128), juce::ModifierKeys(r.nextInt(8)), 0)); break;
                case 3: processor.setEasyMode(!processor.isEasyMode()); break;
                case 4: renderBlocks(processor, ph, r, pos, 1 + r.nextInt(20), bad); break;
                case 5: pump(5); break;
                default: break;
            }
            ++done;
        }
        renderBlocks(processor, ph, r, pos, 40, bad);
        (void) editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f); // paint every visible view once
        (void) done;
        if (bad != 0)
        {
            std::cerr << "\nUI-FUZZ-FAIL seed=" << seed << ": " << bad << " non-finite output samples\n";
            ++failures;
        }
        editor.reset();
        pump(20);
    }
    std::cout << "\nui fuzz rounds=" << rounds << " distinct controls driven=" << coverage.size() << " failures=" << failures << '\n';
    for (const auto& [k, n] : coverage)
        std::cout << "  " << k << " x" << n << '\n';
    return failures == 0 ? 0 : 1;
}
