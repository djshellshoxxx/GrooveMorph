#pragma once

#include <JuceHeader.h>

#include "ui/ControlMenu.h"

#include <vector>

class GrooveMorphAudioProcessor;

namespace groovemorph::ui
{
// N04 Print to WAV panel (Inspector tab "Print"). Arms a print of the processed output, collects the
// finished buffer from the processor (message thread, ~20 Hz), writes a 24-bit WAV into the per-user
// Prints folder, keeps the last 20 prints listed and offers drag-out, Reveal, Delete and "Use as loop
// source". See docs/specs/N04-PRINT-TO-WAV.md.
class PrintPanel final : public juce::Component, private juce::Timer, private juce::ListBoxModel
{
public:
    explicit PrintPanel(GrooveMorphAudioProcessor& processor);
    ~PrintPanel() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    // <Documents>/GrooveMorph Prints
    static juce::File printsFolder();
    // Writes a stereo 24-bit WAV. False on any failure (a partial file is removed).
    static bool writeWav24(const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate);

private:
    struct Entry
    {
        juce::File file;
        double seconds = 0.0;      // whole file, including any tail
        double sampleRate = 0.0;
        int bars = 0;
        int beatsPerBar = 0;       // 0 = unknown (found on disk): the current meter is assumed
        double bpm = 0.0;          // from the name (rounded) or the exact start tempo
        std::int64_t bodySamples = 0; // 0 = unknown: inferred from bars and bpm
    };
    class Row;

    void timerCallback() override;
    void onPrintClicked();
    void collectFinishedPrint();
    void scanFolder();
    void addEntry(Entry entry);
    const Entry* selectedEntry() const;
    void revealSelected();
    void deleteSelected();
    void useSelectedAsLoop();
    void updateButtons();
    void showMessage(const juce::String& text);

    // ListBoxModel
    int getNumRows() override { return static_cast<int>(entries.size()); }
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow(int row, bool selected, juce::Component* existing) override;
    void selectedRowsChanged(int) override { updateButtons(); }

    GrooveMorphAudioProcessor& processor;
    std::vector<Entry> entries; // newest first, at most printnaming::kMaxListed
    juce::Label title, statusLabel, messageLabel, hint;
    MenuComboBox bars;
    MenuToggleButton wetOnly, includeTail;
    juce::TextButton printButton, revealButton, deleteButton, useButton;
    juce::ListBox list;
};
} // namespace groovemorph::ui
