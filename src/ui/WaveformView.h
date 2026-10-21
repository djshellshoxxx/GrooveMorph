#pragma once

#include <JuceHeader.h>

#include "core/dsp/WaveformPeaks.h"

#include <vector>

namespace groovemorph::ui
{
// Read-only overview of the loop (F09): min/max peak columns from core::computePeaks, the beat/bar grid
// from core::computeBeatGrid and the playhead. Data comes in through setters; nothing here touches the
// audio thread or the processor.
class WaveformView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    WaveformView();
    // Replaces the peak columns (equal-length arrays; both empty = no loop).
    void setPeaks(const std::vector<float>& minimums, const std::vector<float>& maximums);
    // Loop length in meter beats, the timeline beat at which it starts, and the current meter.
    void setGrid(double loopBeats, double originBeats, int beatsPerBar);
    // 0..1 inside the loop, or a negative number to hide the playhead.
    void setPlayhead(double fraction);
    // Number of columns worth computing for the current width.
    [[nodiscard]] int preferredColumns() const noexcept { return juce::jmax(16, getWidth()); }
    void paint(juce::Graphics& g) override;

private:
    std::vector<float> mins, maxs;
    std::vector<groovemorph::core::GridLine> grid;
    double gridBeats = 0.0;
    double gridOrigin = 0.0;
    int gridMeter = 0;
    double playhead = -1.0;
};
} // namespace groovemorph::ui
