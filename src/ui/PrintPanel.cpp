#include "ui/PrintPanel.h"
#include "ui/theme/Theme.h"

#include "PluginProcessor.h"
#include "core/dsp/OutputCapture.h"
#include "core/dsp/PrintNaming.h"

#include <algorithm>
#include <cmath>

namespace pn = groovemorph::core::printnaming;
using OutputState = groovemorph::core::OutputCapture::State;
using OutputFailure = groovemorph::core::OutputCapture::Failure;

namespace groovemorph::ui
{
namespace
{
constexpr int rowHeight = theme::minTarget;

juce::String formatSeconds(double seconds)
{
    return juce::String(seconds, 1) + " s";
}

void setupLabel(juce::Label& label, const juce::String& text, const juce::String& tip = {})
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, theme::text);
    if (tip.isNotEmpty())
        label.setTooltip(tip);
}
} // namespace

// One list row: draws the entry and starts a native file drag (into the DAW) when dragged.
class PrintPanel::Row final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Row(PrintPanel& o) : owner(o) {}

    void update(int newRow)
    {
        row = newRow;
        setTooltip(isValid() ? "Drag this row into your DAW. " + owner.entries[static_cast<std::size_t>(row)].file.getFullPathName()
                             : juce::String());
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        if (!isValid())
            return;
        const auto& e = owner.entries[static_cast<std::size_t>(row)];
        const bool selected = owner.list.isRowSelected(row);
        g.fillAll(selected ? theme::surfaceHover : (row % 2 == 0 ? theme::surface : theme::surfaceRaised));
        if (selected)
        {
            g.setColour(theme::accent);
            g.fillRect(0, 0, 3, getHeight());
        }
        g.setColour(theme::text);
        g.setFont(theme::font(theme::fontBody));
        auto area = getLocalBounds().reduced(6, 0);
        g.drawText(formatSeconds(e.seconds), area.removeFromRight(60), juce::Justification::centredRight);
        g.drawText(e.file.getFileName(), area, juce::Justification::centredLeft, true);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        dragStarted = false;
        if (isValid())
            owner.list.selectRowsBasedOnModifierKeys(row, e.mods, false);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragStarted || !isValid() || e.getDistanceFromDragStart() < 6)
            return;
        const auto& file = owner.entries[static_cast<std::size_t>(row)].file;
        if (!file.existsAsFile())
        {
            owner.showMessage("That file no longer exists.");
            return;
        }
        dragStarted = true;
        juce::StringArray files;
        files.add(file.getFullPathName());
        // Copy (not move): the DAW imports its own copy and the print stays in the list.
        if (!juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, this))
            owner.showMessage("Dragging out is not available here. Use Reveal and drag the file from your file manager.");
    }

private:
    bool isValid() const noexcept { return row >= 0 && row < static_cast<int>(owner.entries.size()); }
    PrintPanel& owner;
    int row = -1;
    bool dragStarted = false;
};

PrintPanel::PrintPanel(GrooveMorphAudioProcessor& p) : processor(p)
{
    setupLabel(title, "Print the processed output to a WAV file");
    title.setFont(theme::font(theme::fontTitle, true));
    setupLabel(statusLabel, "");
    setupLabel(messageLabel, "");
    setupLabel(hint, "Drag a row into your DAW to use the file. Files live in Documents/GrooveMorph Prints.");
    hint.setFont(theme::font(theme::fontLabel));
    hint.setColour(juce::Label::textColourId, theme::textDim);

    for (int i = 1; i <= 16; ++i)
        bars.addItem(juce::String(i) + (i == 1 ? " bar" : " bars"), i);
    bars.setSelectedId(4, juce::dontSendNotification);
    bars.setTooltip("Length of the print in bars (1-16), using the current beats per bar. It starts on the next bar line while the transport plays and is cut at exactly this length. Prints longer than 90 seconds are refused.");

    wetOnly.setButtonText("Wet only");
    wetOnly.setTooltip("Print only the effect signal, without the dry signal. Needs Intensity x Dry/Wet above 1%; otherwise the full output is printed. With band split or Bypass active this is approximate.");
    includeTail.setButtonText("Include tail");
    includeTail.setTooltip("Adds the next few seconds of output after the last bar (the engine's tail length), so replayed or reversed audio can finish. The tail is outside the bar-aligned loop.");

    printButton.setButtonText("Print");
    printButton.setTooltip("Records the output for the chosen number of bars, starting on the next bar line while the transport plays. Stops and discards the print if the transport stops, jumps, or the tempo changes. Click again to cancel.");
    printButton.onClick = [this] { onPrintClicked(); };
    revealButton.setButtonText("Reveal");
    revealButton.setTooltip("Shows the selected file in your file manager.");
    revealButton.onClick = [this] { revealSelected(); };
    deleteButton.setButtonText("Delete");
    deleteButton.setTooltip("Moves the selected file to the trash (deletes it if there is no trash).");
    deleteButton.onClick = [this] { deleteSelected(); };
    useButton.setButtonText("Use as loop source");
    useButton.setTooltip("Loads the selected print (without its tail) as the beat-locked loop and switches the input source to it, so you can resample it. Loops are limited to 20 seconds.");
    useButton.onClick = [this] { useSelectedAsLoop(); };

    list.setModel(this);
    list.setRowHeight(rowHeight);
    list.setColour(juce::ListBox::backgroundColourId, theme::bg);
    list.setMultipleSelectionEnabled(false);

    for (auto* c : std::initializer_list<juce::Component*>{&title, &statusLabel, &messageLabel, &hint, &bars, &wetOnly,
                                                           &includeTail, &printButton, &revealButton, &deleteButton,
                                                           &useButton, &list})
        addAndMakeVisible(*c);

    scanFolder();
    updateButtons();
    startTimerHz(20);
}

PrintPanel::~PrintPanel()
{
    stopTimer();
    list.setModel(nullptr);
}

juce::File PrintPanel::printsFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("GrooveMorph Prints");
}

bool PrintPanel::writeWav24(const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
{
    if (audio.getNumChannels() < 1 || audio.getNumSamples() < 1 || !(sampleRate > 0.0))
        return false;
    bool ok = false;
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream = file.createOutputStream();
        if (stream != nullptr)
        {
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(stream.get(), sampleRate, 2u, 24, juce::StringPairArray(), 0));
            if (writer != nullptr)
            {
                stream.release(); // the writer owns the stream now
                ok = writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
            }
        }
    } // the writer is destroyed here: flushed and closed
    if (!ok)
        file.deleteFile();
    return ok;
}

void PrintPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::surface);
}

void PrintPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto next = [&area](int h = theme::minTarget) {
        auto r = area.removeFromTop(h);
        area.removeFromTop(4);
        return r;
    };
    title.setBounds(next());
    auto r = next();
    bars.setBounds(r.removeFromLeft(100));
    wetOnly.setBounds(r.removeFromLeft(100).reduced(6, 0));
    includeTail.setBounds(r.removeFromLeft(120).reduced(6, 0));
    printButton.setBounds(r.removeFromLeft(110).reduced(4, 0));
    statusLabel.setBounds(next());
    messageLabel.setBounds(next());
    auto bottom = area.removeFromBottom(theme::minTarget);
    area.removeFromBottom(4);
    hint.setBounds(area.removeFromBottom(theme::space5));
    list.setBounds(area);
    revealButton.setBounds(bottom.removeFromLeft(90).reduced(2, 0));
    deleteButton.setBounds(bottom.removeFromLeft(90).reduced(2, 0));
    useButton.setBounds(bottom.removeFromLeft(160).reduced(2, 0));
}

juce::Component* PrintPanel::refreshComponentForRow(int row, bool, juce::Component* existing)
{
    auto* component = dynamic_cast<Row*>(existing);
    if (component == nullptr)
    {
        delete existing;
        component = new Row(*this);
    }
    component->update(row);
    return component;
}

void PrintPanel::showMessage(const juce::String& text)
{
    messageLabel.setText(text, juce::dontSendNotification);
}

const PrintPanel::Entry* PrintPanel::selectedEntry() const
{
    const int row = list.getSelectedRow();
    return row >= 0 && row < static_cast<int>(entries.size()) ? &entries[static_cast<std::size_t>(row)] : nullptr;
}

void PrintPanel::updateButtons()
{
    const auto* entry = selectedEntry();
    revealButton.setEnabled(entry != nullptr);
    deleteButton.setEnabled(entry != nullptr);
    useButton.setEnabled(entry != nullptr);
}

void PrintPanel::onPrintClicked()
{
    if (processor.getPrintState() != OutputState::Idle)
    {
        processor.cancelPrint();
        showMessage("Print cancelled.");
        return;
    }
    showMessage({});
    if (!processor.armPrint(bars.getSelectedId(), wetOnly.getToggleState(), includeTail.getToggleState()))
    {
        const auto busy = processor.captureConflict(GrooveMorphAudioProcessor::CaptureKind::Print);
        showMessage(busy.isNotEmpty() ? busy : juce::String("The print could not start (not ready, or out of memory)."));
    }
}

void PrintPanel::timerCallback()
{
    const auto state = processor.getPrintState();
    switch (state)
    {
        case OutputState::Idle:
            printButton.setButtonText("Print");
            statusLabel.setText("Ready. Start the transport, then press Print.", juce::dontSendNotification);
            break;
        case OutputState::Armed:
            printButton.setButtonText("Cancel");
            statusLabel.setText("Waiting for the next bar line (the transport must be playing)...", juce::dontSendNotification);
            break;
        case OutputState::Recording:
            printButton.setButtonText("Cancel");
            statusLabel.setText("Recording " + juce::String(juce::roundToInt(processor.getPrintProgress() * 100.0)) + "%",
                                juce::dontSendNotification);
            break;
        case OutputState::Done:
            statusLabel.setText("Writing the file...", juce::dontSendNotification);
            collectFinishedPrint();
            break;
        case OutputState::Aborted:
        {
            switch (processor.getPrintFailure())
            {
                case OutputFailure::TooLong: showMessage("Print refused: that many bars would exceed 90 seconds at this tempo."); break;
                case OutputFailure::TransportStopped: showMessage("Print discarded: the transport stopped while recording."); break;
                case OutputFailure::Jumped: showMessage("Print discarded: the transport jumped (seek or loop) while recording."); break;
                case OutputFailure::TempoChanged: showMessage("Print discarded: the tempo changed while recording."); break;
                case OutputFailure::None: showMessage("Print stopped."); break;
            }
            processor.cancelPrint(); // acknowledge: back to Idle and free the memory
            break;
        }
    }
    const bool idle = state == OutputState::Idle;
    bars.setEnabled(idle);
    wetOnly.setEnabled(idle);
    includeTail.setEnabled(idle);
}

void PrintPanel::collectFinishedPrint()
{
    GrooveMorphAudioProcessor::PrintResult result;
    if (!processor.takePrintResult(result))
        return;

    const auto folder = printsFolder();
    if (!folder.createDirectory().wasOk())
    {
        showMessage("Could not create " + folder.getFullPathName());
        return;
    }
    const auto now = juce::Time::getCurrentTime();
    pn::Timestamp ts;
    ts.year = now.getYear();
    ts.month = now.getMonth() + 1;
    ts.day = now.getDayOfMonth();
    ts.hour = now.getHours();
    ts.minute = now.getMinutes();
    ts.second = now.getSeconds();
    const auto baseName = pn::buildPrintFileName(result.sceneName.toStdString(), result.bpm, result.bars, ts);
    juce::File file;
    for (int n = 1; n < 1000; ++n)
    {
        file = folder.getChildFile(juce::String(pn::withCollisionSuffix(baseName, n)));
        if (!file.exists())
            break;
    }
    if (file.exists() || !writeWav24(file, result.audio, result.sampleRate))
    {
        showMessage("Could not write " + file.getFullPathName());
        return;
    }

    Entry entry;
    entry.file = file;
    entry.sampleRate = result.sampleRate;
    entry.seconds = static_cast<double>(result.audio.getNumSamples()) / result.sampleRate;
    entry.bars = result.bars;
    entry.beatsPerBar = result.beatsPerBar;
    entry.bpm = result.bpm;
    entry.bodySamples = result.bodySamples;
    addEntry(entry);
    juce::String text = "Saved " + file.getFileName();
    if (result.wetOnly && !result.wetOnlyApplied)
        text += " (Intensity x Dry/Wet was below 1% for part of it: those parts are full output)";
    showMessage(text);
}

void PrintPanel::addEntry(Entry entry)
{
    entries.insert(entries.begin(), std::move(entry));
    if (entries.size() > static_cast<std::size_t>(pn::kMaxListed))
        entries.resize(static_cast<std::size_t>(pn::kMaxListed)); // older files stay on disk
    list.updateContent();
    list.selectRow(0);
    list.repaint();
    updateButtons();
}

void PrintPanel::scanFolder()
{
    const auto folder = printsFolder();
    if (!folder.isDirectory())
        return;
    auto files = folder.findChildFiles(juce::File::findFiles, false, "GrooveMorph_*.wav");
    std::vector<std::pair<juce::Time, juce::File>> dated;
    for (const auto& f : files)
        if (pn::isPrintFileName(f.getFileName().toStdString()))
            dated.emplace_back(f.getLastModificationTime(), f);
    std::sort(dated.begin(), dated.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (dated.size() > static_cast<std::size_t>(pn::kMaxListed))
        dated.resize(static_cast<std::size_t>(pn::kMaxListed));

    juce::WavAudioFormat wav;
    entries.clear();
    for (const auto& item : dated)
    {
        const auto parsed = pn::parsePrintFileName(item.second.getFileName().toStdString());
        Entry entry;
        entry.file = item.second;
        entry.bars = parsed.valid ? parsed.bars : 0;
        entry.bpm = parsed.valid ? parsed.bpm : 0.0;
        if (std::unique_ptr<juce::FileInputStream> in = item.second.createInputStream())
        {
            std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(in.get(), false));
            if (reader != nullptr && reader->sampleRate > 0.0)
            {
                entry.sampleRate = reader->sampleRate;
                entry.seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            }
        }
        entries.push_back(std::move(entry));
    }
    list.updateContent();
}

void PrintPanel::revealSelected()
{
    if (const auto* entry = selectedEntry())
        entry->file.revealToUser();
}

void PrintPanel::deleteSelected()
{
    const int row = list.getSelectedRow();
    const auto* entry = selectedEntry();
    if (entry == nullptr)
        return;
    const auto file = entry->file;
    juce::String text = "Moved " + file.getFileName() + " to the trash.";
    if (file.existsAsFile() && !file.moveToTrash())
    {
        if (!file.deleteFile())
        {
            showMessage("Could not delete " + file.getFileName());
            return;
        }
        text = "Deleted " + file.getFileName() + " (no trash available).";
    }
    entries.erase(entries.begin() + row);
    list.updateContent();
    list.deselectAllRows();
    list.repaint();
    updateButtons();
    showMessage(text);
}

void PrintPanel::useSelectedAsLoop()
{
    const auto* entry = selectedEntry();
    if (entry == nullptr)
        return;
    if (!entry->file.existsAsFile())
    {
        showMessage("That file no longer exists.");
        return;
    }
    if (entry->bars < 1 || !(entry->bpm > 0.0))
    {
        showMessage("This file's name does not say its bars and tempo, so it cannot be used as a beat-locked loop here. Use Input > Import instead.");
        return;
    }
    const int beatsPerBar = entry->beatsPerBar > 0 ? entry->beatsPerBar : processor.getGridContext().beatsPerBar;
    const double bodyBeats = static_cast<double>(entry->bars) * beatsPerBar;
    // Exact body for prints made this session; inferred from the name's (rounded) tempo for files found on disk.
    const double bodySeconds = entry->bodySamples > 0 && entry->sampleRate > 0.0
        ? static_cast<double>(entry->bodySamples) / entry->sampleRate
        : bodyBeats * 60.0 / entry->bpm;
    const double nativeBpm = bodyBeats * 60.0 / bodySeconds;
    const auto result = processor.importLoopFile(entry->file, 0, nativeBpm, bodySeconds);
    if (result.failed())
    {
        showMessage("Could not use as loop: " + result.getErrorMessage());
        return;
    }
    processor.setSourceMode(2);
    showMessage("Loaded as the loop source (" + juce::String(entry->bars) + " bars, tail removed). The input source is now Captured / imported loop.");
}
} // namespace groovemorph::ui
