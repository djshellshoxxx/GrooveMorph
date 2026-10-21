// Developer tool: renders the real GrooveMorph editor (software rendering, no display needed for
// the snapshot itself) to PNG files so the layout can be inspected. Usage: EditorSnapshot <outdir>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/visual/VisualWindow.h"

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
    auto* editor = dynamic_cast<GrooveMorphAudioProcessorEditor*>(processor.createEditor());
    editor->setVisible(true);
    editor->setSize(1290, 784);
    pump(200);
    const char* names[] = {"placement", "generator", "scenes", "morph", "limits", "input", "perform", "status"};
    for (int i = 0; i < 8; ++i)
    {
        editor->showInspectorTab(i);
        pump(120);
        save(*editor, out.getChildFile(juce::String("editor-") + names[i] + ".png"));
    }
    // Narrowest supported size.
    editor->showInspectorTab(0);
    editor->setSize(1230, 744);
    pump(120);
    save(*editor, out.getChildFile("editor-minimum-size.png"));
    delete editor;

    // Easy mode (N05) at the default and the minimum size.
    processor.setEasyMode(true);
    {
        auto* easyEditor = dynamic_cast<GrooveMorphAudioProcessorEditor*>(processor.createEditor());
        easyEditor->setVisible(true);
        easyEditor->setSize(1290, 740);
        save(*easyEditor, out.getChildFile("editor-easy.png"));
        easyEditor->setSize(1230, 700);
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
