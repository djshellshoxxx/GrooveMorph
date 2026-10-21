#include "ui/WaveformView.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::ui
{
WaveformView::WaveformView()
{
    setOpaque(true);
    setTooltip("Overview of the loop. Thin lines are beats, bright lines are bars, the white cursor is the playhead.");
}

void WaveformView::setPeaks(const std::vector<float>& minimums, const std::vector<float>& maximums)
{
    if (minimums.size() != maximums.size())
        return;
    if (minimums == mins && maximums == maxs)
        return;
    mins = minimums;
    maxs = maximums;
    repaint();
}

void WaveformView::setGrid(double loopBeats, double originBeats, int beatsPerBar)
{
    if (loopBeats == gridBeats && originBeats == gridOrigin && beatsPerBar == gridMeter)
        return;
    gridBeats = loopBeats;
    gridOrigin = originBeats;
    gridMeter = beatsPerBar;
    grid = groovemorph::core::computeBeatGrid(loopBeats, originBeats, beatsPerBar);
    repaint();
}

void WaveformView::setPlayhead(double fraction)
{
    const double next = std::isfinite(fraction) && fraction >= 0.0 ? std::min(fraction, 1.0) : -1.0;
    if (std::abs(next - playhead) < 1.0e-4)
        return;
    playhead = next;
    repaint();
}

void WaveformView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xff10161d));
    const float w = static_cast<float>(bounds.getWidth());
    const float h = static_cast<float>(bounds.getHeight());
    if (w < 4.0f || h < 4.0f)
        return;

    if (mins.empty())
    {
        g.setColour(juce::Colours::grey);
        g.setFont(13.0f);
        g.drawText("No loop loaded", bounds, juce::Justification::centred);
        return;
    }

    // Grid under the waveform: beats dim, bars brighter.
    for (const auto& line : grid)
    {
        const int x = static_cast<int>(std::round(static_cast<float>(line.fraction) * w));
        g.setColour(line.bar ? juce::Colour(0xff5f7388) : juce::Colour(0xff2a3744));
        g.drawVerticalLine(juce::jlimit(0, bounds.getWidth() - 1, x), 0.0f, h);
    }

    const float mid = h * 0.5f;
    const float scale = mid - 2.0f;
    const auto columns = static_cast<std::int64_t>(mins.size());
    g.setColour(juce::Colour(0xff4fc3f7));
    for (int x = 0; x < bounds.getWidth(); ++x)
    {
        const auto column = static_cast<std::size_t>(juce::jlimit<std::int64_t>(
            0, columns - 1, static_cast<std::int64_t>(x) * columns / bounds.getWidth()));
        const float hi = juce::jlimit(-1.0f, 1.0f, maxs[column]);
        const float lo = juce::jlimit(-1.0f, 1.0f, mins[column]);
        const float top = mid - hi * scale;
        const float bottom = mid - lo * scale;
        g.fillRect(static_cast<float>(x), top, 1.0f, std::max(1.0f, bottom - top));
    }

    if (playhead >= 0.0)
    {
        g.setColour(juce::Colours::white);
        g.drawVerticalLine(juce::jlimit(0, bounds.getWidth() - 1, static_cast<int>(playhead * w)), 0.0f, h);
    }
}
} // namespace groovemorph::ui
