// Developer tool: renders the real GrooveMorph editor (software rendering, no display needed for
// the snapshot itself) to PNG files so the layout can be inspected. Usage: EditorSnapshot <outdir>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/PresetBar.h"
#include "ui/visual/VisualWindow.h"

#include <cmath>
#include <functional>
#include <iostream>

namespace
{
class FakePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

void pump(int) {} // no message loop is run; the editor refreshes synchronously on construction

void save(juce::Component& component, const juce::File& file)
{
    const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat png;
    if (stream.openedOk())
        png.writeImageToStream(image, stream);
    std::cout << "wrote " << file.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out(argc > 1 ? argv[1] : ".");
    out.createDirectory();

    GrooveMorphAudioProcessor processor;
    FakePlayHead playHead;
    processor.setPlayHead(&playHead);
    processor.prepareToPlay(48000.0, 512);
    playHead.info.setBpm(120.0);
    playHead.info.setTimeSignature(juce::AudioPlayHead::TimeSignature{4, 4});
    playHead.info.setIsPlaying(true);
    playHead.info.setTimeInSamples(100000);
    playHead.info.setPpqPosition(100000 / 24000.0);
    playHead.info.setPpqPositionOfLastBarStart(4.0);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    buffer.clear();
    processor.processBlock(buffer, midi);

    // A scene that exercises the inspector: placement, events, a lock, an activity limit.
    namespace ctl = groovemorph::core::control;
    auto settings = processor.getSceneBank().variation(0).settings;
    settings.operations[2] = {true, 0.8f, 0.125f};
    settings.operations[3] = {true, 1.0f, 1.0f};
    settings.control.placement.enabled = true;
    settings.control.placement.window = ctl::WindowMode::Custom;
    settings.control.placement.startPhase = 0.5f;
    settings.control.placement.spanPhase = 0.375f;
    settings.control.placement.every = 2;
    settings.control.placement.downbeatProtectBeats = 0.5f;
    settings.control.lane.explicitEvents = true;
    const auto pt = processor.getGridContext().phraseTicks();
    for (const auto& e : {ctl::LaneEvent{0, true, 0, 4 * 960, 1.0f}, ctl::LaneEvent{2, true, 8 * 960, 2 * 960, 0.6f},
                          ctl::LaneEvent{3, true, 12 * 960, 4 * 960, 1.0f}, ctl::LaneEvent{2, true, 5 * 960, 960, 1.0f}})
        (void)ctl::addEvent(settings.control.lane, e, pt);
    settings.control.permissions.operationLocks[1] = ctl::lock::depth | ctl::lock::slice;
    settings.control.activity.enabled = true;
    settings.control.activity.maxConcurrent = 2;
    processor.setVariationSettings(0, settings);
    processor.setDemoSourceEnabled(true);

    // A fresh instance opens in Easy mode (N05); the inspector shots below need the full view.
    processor.setEasyMode(false);
    // Loop with a waveform for the Input tab (a decaying click train).
    {
        const auto wav = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gm-snapshot-loop.wav");
        wav.deleteFile();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::FileOutputStream> stream(wav.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), 48000.0, 2, 24, {}, 0));
        if (writer != nullptr)
        {
            stream.release();
            juce::AudioBuffer<float> b(2, 96000);
            for (int i = 0; i < 96000; ++i)
            {
                const float env = std::exp(-static_cast<float>(i % 12000) / 2500.0f);
                const float v = 0.8f * env * std::sin(0.07f * static_cast<float>(i));
                b.setSample(0, i, v);
                b.setSample(1, i, v);
            }
            writer->writeFromAudioSampleBuffer(b, 0, 96000);
        }
        writer.reset(); // flush the file before importing it
        std::cout << "loop import: " << (processor.importLoopFile(wav, 4).wasOk() ? "ok" : "FAILED") << "\n";
    }

    auto chainVisible = [](juce::Component& c) {
        for (auto* p = &c; p != nullptr; p = p->getParentComponent())
            if (!p->isVisible())
                return false;
        return true;
    };
    auto clickButton = [&chainVisible](juce::Component& root, const juce::String& text) {
        std::function<bool(juce::Component&)> find = [&](juce::Component& c) -> bool {
            if (auto* b = dynamic_cast<juce::Button*>(&c))
                if (b->getButtonText() == text && chainVisible(*b))
                {
                    if (b->getClickingTogglesState())
                        b->setToggleState(!b->getToggleState(), juce::sendNotificationSync);
                    else
                        { if (b->onClick) b->onClick(); } // triggerClick() is asynchronous; no message loop runs here
                    return true;
                }
            for (auto* child : c.getChildren())
                if (find(*child))
                    return true;
            return false;
        };
        return find(root);
    };

    auto* editor = dynamic_cast<GrooveMorphAudioProcessorEditor*>(processor.createEditor());
    editor->setVisible(true);
    editor->setSize(1290, 784);
    pump(200);
    const char* names[] = {"place", "generator", "scenes", "morph", "limit", "input", "perform", "info", "print"};
    for (int i = 0; i < 9; ++i)
    {
        editor->showInspectorTab(i);
        pump(120);
        save(*editor, out.getChildFile(juce::String("editor-") + names[i] + ".png"));
    }
    editor->showInspectorTab(0);
    (void) clickButton(*editor, "Euclid / chance");
    editor->setSize(1291, 784); // re-run layout so the revealed panel gets bounds
    editor->setSize(1290, 784);
    save(*editor, out.getChildFile("editor-place-euclid.png"));
    (void) clickButton(*editor, "Euclid / chance");
    // Input tab with the loop source selected so the waveform is drawn.
    processor.setSourceMode(2);
    editor->showInspectorTab(5);
    save(*editor, out.getChildFile("editor-input-waveform.png"));
    processor.setSourceMode(1);

    // Narrowest supported size, every tab.
    editor->setSize(1230, 744);
    pump(120);
    for (int i = 0; i < 9; ++i)
    {
        editor->showInspectorTab(i);
        save(*editor, out.getChildFile(juce::String("editor-min-") + names[i] + ".png"));
    }
    editor->showInspectorTab(0);
    (void) clickButton(*editor, "Euclid / chance");
    editor->setSize(1231, 744);
    editor->setSize(1230, 744);
    save(*editor, out.getChildFile("editor-min-place-euclid.png"));
    delete editor;

    // Preset browser (the dropdown behind the preset bar), rendered on its own.
    {
        groovemorph::ui::PresetBar bar(processor);
        if (auto browser = bar.createBrowserContent())
        {
            if (browser->getWidth() == 0)
                browser->setSize(420, 360);
            save(*browser, out.getChildFile("preset-browser.png"));
        }
    }

    // Easy mode (N05): first-run guide (unseen flag), then the plain view, at the default and minimum size.
    processor.setEasyMode(true);
    {
        auto* easyEditor = dynamic_cast<GrooveMorphAudioProcessorEditor*>(processor.createEditor());
        easyEditor->setVisible(true);
        easyEditor->setSize(1290, 784);
        save(*easyEditor, out.getChildFile("editor-easy-guide.png"));
        easyEditor->setSize(1230, 744);
        save(*easyEditor, out.getChildFile("editor-easy-guide-minimum-size.png"));
        for (int i = 0; i < 3; ++i) // reach the last guide step for one more shot
            (void) clickButton(*easyEditor, "Next");
        save(*easyEditor, out.getChildFile("editor-easy-guide-last-step.png"));
        (void) clickButton(*easyEditor, "Skip guide");
        (void) clickButton(*easyEditor, "Done");
        easyEditor->setSize(1290, 784);
        save(*easyEditor, out.getChildFile("editor-easy.png"));
        easyEditor->setSize(1230, 744);
        save(*easyEditor, out.getChildFile("editor-easy-minimum-size.png"));
        delete easyEditor;
    }
    processor.setEasyMode(false);

    // Visual window: drive the real processor for several seconds (demo loop, trace active), then render the
    // content component. Two scenes: the busy inspector scene above and a plain repeat + reverse scene.
    namespace vis = groovemorph::ui::visual;
    auto runSeconds = [&](double seconds, std::int64_t& position) {
        const int blocks = static_cast<int>(seconds * 48000.0 / 512.0);
        for (int b = 0; b < blocks; ++b)
        {
            playHead.info.setTimeInSamples(position);
            playHead.info.setPpqPosition(static_cast<double>(position) / 24000.0);
            playHead.info.setPpqPositionOfLastBarStart(std::floor(static_cast<double>(position) / 96000.0) * 4.0);
            buffer.clear();
            processor.processBlock(buffer, midi);
            position += 512;
        }
    };
    auto visualShot = [&](const char* name, const groovemorph::core::VariationSettings& scene, int width, int height) {
        processor.setVariationSettings(0, scene);
        vis::VisualContent content(processor);
        content.setSize(width, height);
        content.setActive(true);
        std::int64_t position = 0;
        runSeconds(9.0, position);
        content.refresh();
        save(content, out.getChildFile(name));
        std::cout << content.ledgerText() << "\n";
        content.setActive(false);
    };
    {
        groovemorph::core::VariationSettings plain;
        plain.operations[0] = {true, 1.0f, 0.25f};
        plain.operations[1] = {true, 1.0f, 0.5f};
        visualShot("visual-repeat-reverse.png", plain, 1100, 680);
        groovemorph::core::VariationSettings busy;
        busy.operations[0] = {true, 0.8f, 0.25f};
        busy.operations[2] = {true, 0.8f, 0.5f};
        busy.operations[3] = {true, 1.0f, 1.0f};
        busy.operations[4] = {true, 0.9f, 1.0f};
        visualShot("visual-halftime-tapestop-gate.png", busy, 1100, 680);
        visualShot("visual-small.png", plain, 800, 500);
        // Placement window + event lane scene at the default window size: rings show lap structure.
        groovemorph::core::VariationSettings placed = busy;
        placed.control.placement.enabled = true;
        placed.control.placement.window = ctl::WindowMode::Custom;
        placed.control.placement.startPhase = 0.25f;
        placed.control.placement.spanPhase = 0.5f;
        visualShot("visual-wide.png", placed, 1280, 720);
    }
    processor.setPlayHead(nullptr);
    return 0;
}
