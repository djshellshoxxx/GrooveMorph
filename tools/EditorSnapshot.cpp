// Developer tool: renders the real GrooveMorph editor (software rendering, no display needed for
// the snapshot itself) to PNG files so the layout can be inspected. Usage: EditorSnapshot [outdir] (default docs/beta/screenshots).
// Exits non-zero if any PNG is missing, empty or a single flat colour.
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

int failures = 0; // PNGs that are missing, empty or one flat colour make the tool exit non-zero

bool isFlat(const juce::Image& image)
{
    if (!image.isValid() || image.getWidth() <= 0 || image.getHeight() <= 0)
        return true;
    const auto first = image.getPixelAt(0, 0);
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt(x, y) != first)
                return false;
    return true;
}

void save(juce::Component& component, const juce::File& file)
{
    const auto bounds = component.getLocalBounds();
    if (bounds.isEmpty())
    {
        std::cout << "FAIL empty bounds for " << file.getFileName() << "\n";
        ++failures;
        return;
    }
    const auto image = component.createComponentSnapshot(bounds, true, 1.0f);
    if (isFlat(image))
    {
        std::cout << "FAIL empty or flat image for " << file.getFileName() << "\n";
        ++failures;
        return;
    }
    file.deleteFile();
    {
        juce::FileOutputStream stream(file);
        juce::PNGImageFormat png;
        if (!stream.openedOk() || !png.writeImageToStream(image, stream))
        {
            std::cout << "FAIL could not write " << file.getFullPathName() << "\n";
            ++failures;
            return;
        }
    }
    std::cout << "wrote " << file.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";
}

// Resize like a host would. The editor clamps to its own limits, so the PNG may differ from the request;
// that is reported, not fatal.
void resizeEditor(juce::Component& editor, int w, int h)
{
    editor.setSize(w + 1, h); // force a layout pass even when the size is unchanged
    editor.setSize(w, h);
    if (editor.getWidth() != w || editor.getHeight() != h)
        std::cout << "note: requested " << w << "x" << h << ", editor is " << editor.getWidth() << "x"
                  << editor.getHeight() << "\n";
}

GrooveMorphAudioProcessorEditor* openEditor(GrooveMorphAudioProcessor& processor)
{
    auto* editor = dynamic_cast<GrooveMorphAudioProcessorEditor*>(processor.createEditor());
    if (editor == nullptr)
        std::cout << "FAIL createEditor did not return a GrooveMorphAudioProcessorEditor\n";
    else
        editor->setVisible(true);
    return editor;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "docs/beta/screenshots"));
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

    const char* names[] = {"place", "generator", "scenes", "morph", "limit", "input", "perform", "info", "print"};
    const auto sized = [](const char* prefix, int w, int h) { return juce::String(prefix) + juce::String(w) + "x" + juce::String(h) + ".png"; };

    // Studio editor at the four sizes of UR-01.
    if (auto* editor = openEditor(processor))
    {
        editor->showInspectorTab(0);
        const int sizes[][2] = {{960, 640}, {1180, 760}, {1600, 1000}, {2200, 1400}};
        for (const auto& sz : sizes)
        {
            resizeEditor(*editor, sz[0], sz[1]);
            save(*editor, out.getChildFile(sized("studio-", sz[0], sz[1])));
        }

        // Every inspector tab at the default size.
        resizeEditor(*editor, 1180, 760);
        for (int i = 0; i < 9; ++i)
        {
            if (i == 5)
                processor.setSourceMode(2); // loop source so the Input tab draws the waveform
            editor->showInspectorTab(i);
            save(*editor, out.getChildFile(juce::String("tab-") + names[i] + ".png"));
            if (i == 5)
                processor.setSourceMode(1);
        }

        // Minimum size, every tab.
        resizeEditor(*editor, 960, 640);
        for (int i = 0; i < 9; ++i)
        {
            editor->showInspectorTab(i);
            save(*editor, out.getChildFile(juce::String("tab-min-") + names[i] + ".png"));
        }
        editor->showInspectorTab(0);
        delete editor;
    }

    // Preset browser (the dropdown behind the preset bar), rendered on its own.
    {
        groovemorph::ui::PresetBar bar(processor);
        if (auto browser = bar.createBrowserContent())
        {
            if (browser->getWidth() == 0)
                browser->setSize(420, 360);
            save(*browser, out.getChildFile("preset-browser.png"));
        }
        else
        {
            std::cout << "FAIL no preset browser content\n";
            ++failures;
        }
    }

    // Easy mode (N05) through the processor API: default and minimum size.
    processor.setEasyMode(true);
    if (auto* easyEditor = openEditor(processor))
    {
        resizeEditor(*easyEditor, 1180, 760);
        save(*easyEditor, out.getChildFile("easy-1180x760.png"));
        resizeEditor(*easyEditor, 960, 640);
        save(*easyEditor, out.getChildFile("easy-960x640.png"));
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
    if (failures > 0)
        std::cout << failures << " snapshot(s) failed\n";
    return failures > 0 ? 1 : 0;
}
