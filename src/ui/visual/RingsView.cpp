#include "ui/visual/RingsView.h"

#include <cmath>

namespace groovemorph::ui::visual
{
namespace viz = groovemorph::core::viz;

namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;

// Effect priority when several are active in one cell (as in the Warp Weave).
viz::Effect primaryOfCell(const viz::LapCell& c)
{
    if (c.frames == 0)
        return viz::Effect::Live;
    if (c.active(4)) return viz::Effect::TapeStop;
    if (c.active(3)) return viz::Effect::HalfTime;
    if (c.active(1)) return viz::Effect::Reverse;
    if (c.active(0)) return viz::Effect::Repeat;
    return viz::Effect::Live;
}

void arc(juce::Graphics& g, float cx, float cy, float r, float f0, float f1, float thickness)
{
    juce::Path p;
    p.addCentredArc(cx, cy, r, r, 0.0f, f0 * twoPi, f1 * twoPi, true);
    g.strokePath(p, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
}
} // namespace

RingsView::RingsView()
{
    setTitle("Phrase rings");
    setDescription("One ring per lap of the phrase, newest outermost: repeats line up, changes break the pattern.");
}

void RingsView::setData(Data d)
{
    data = std::move(d);
    juce::String text = "Phrase rings: " + juce::String(static_cast<int>(data.lapCount)) + " laps recorded. ";
    if (data.lapCount >= 2)
    {
        int differing = 0;
        for (auto m : data.diffNewest.mask)
            if (m != 0)
                ++differing;
        text << (differing == 0 ? "The newest lap matches the previous lap so far." : juce::String(differing) + " positions differ from the previous lap.");
    }
    setDescription(text);
    repaint();
}

void RingsView::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour(palette::panel);
    g.fillRoundedRectangle(b, 10.0f);
    g.setColour(palette::line);
    g.drawRoundedRectangle(b.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(palette::ink);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText("PHRASE RINGS", 16, 10, getWidth() - 32, 20, juce::Justification::centredLeft);
    g.setColour(palette::dim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("one ring per lap, newest outermost", 16, 30, getWidth() - 32, 16, juce::Justification::centredLeft);

    const float cx = b.getCentreX();
    const float cy = b.getY() + 52.0f + (b.getHeight() - 52.0f - 22.0f) * 0.5f;
    const float outer = juce::jmin(b.getWidth() * 0.5f - 22.0f, (b.getHeight() - 52.0f - 22.0f) * 0.5f - 10.0f);
    const std::size_t shown = juce::jlimit<std::size_t>(1, 4, data.lapCount == 0 ? 1 : data.lapCount);
    const float track = juce::jmin(18.0f, (outer - 14.0f) / static_cast<float>(shown + 1));
    const float gapPx = 3.0f;

    // Placement: where nothing may act is darker (cells decided by the same evaluate() the engine uses).
    if (data.placementActive)
    {
        g.setColour(palette::slate.withAlpha(0.75f));
        for (std::size_t c = 0; c < viz::lapCells; ++c)
            if (!data.allowed[c])
                arc(g, cx, cy, outer - track * 0.5f - (static_cast<float>(shown) * (track + gapPx)) * 0.5f + track,
                    static_cast<float>(c) / static_cast<float>(viz::lapCells), static_cast<float>(c + 1) / static_cast<float>(viz::lapCells),
                    static_cast<float>(shown) * (track + gapPx) + 6.0f);
    }

    for (std::size_t k = 0; k < shown; ++k)
    {
        const float r = outer - track * 0.5f - static_cast<float>(k) * (track + gapPx);
        // Track background.
        g.setColour(palette::line);
        arc(g, cx, cy, r, 0.0f, 0.9999f, track);
        if (k >= data.lapCount)
            continue;
        const auto& lap = data.laps[k];
        const float alpha = k == 0 ? 1.0f : juce::jmax(0.3f, 0.75f - 0.18f * static_cast<float>(k));
        for (std::size_t c = 0; c < viz::lapCells; ++c)
        {
            const auto& cell = lap.cells[c];
            if (cell.frames == 0)
                continue;
            const float f0 = static_cast<float>(c) / static_cast<float>(viz::lapCells);
            const float f1 = static_cast<float>(c + 1) / static_cast<float>(viz::lapCells);
            const auto effect = primaryOfCell(cell);
            g.setColour(effectColour(effect).withAlpha(effect == viz::Effect::Live ? alpha * 0.35f : alpha));
            arc(g, cx, cy, r, f0, f1 + 0.0015f, track);
            if (cell.active(2)) // gate closed: a dotted inner track so it reads without colour
            {
                g.setColour(effectColour(viz::Effect::Gate).withAlpha(alpha));
                juce::Path p;
                p.addCentredArc(cx, cy, r - track * 0.28f, r - track * 0.28f, 0.0f, f0 * twoPi, f1 * twoPi, true);
                juce::Path dashed;
                const float dashes[] = {1.5f, 2.5f};
                juce::PathStrokeType(2.0f).createDashedStroke(dashed, p, dashes, 2);
                g.fillPath(dashed);
            }
        }
    }

    // Pencil: the planned rest of the newest lap, drawn as outlines on the newest ring.
    if (data.timed)
    {
        const float r = outer - track * 0.5f;
        const std::size_t from = static_cast<std::size_t>(juce::jlimit(0.0, 1.0, data.nowFraction) * static_cast<double>(viz::lapCells));
        for (std::size_t c = from; c < viz::lapCells; ++c)
        {
            if (data.planned[c] == 255)
                continue;
            const auto effect = static_cast<viz::Effect>(data.planned[c]);
            if (effect == viz::Effect::Live)
                continue;
            g.setColour(effectColour(effect).withAlpha(0.45f));
            arc(g, cx, cy, r, static_cast<float>(c) / static_cast<float>(viz::lapCells), static_cast<float>(c + 1) / static_cast<float>(viz::lapCells) + 0.0015f, 2.0f);
        }
    }

    // Differences from the previous lap: a small tick outside the outer ring.
    if (data.lapCount >= 2)
    {
        for (std::size_t c = 0; c < viz::lapCells; ++c)
        {
            if (data.diffNewest.mask[c] == 0)
                continue;
            const float a = (static_cast<float>(c) + 0.5f) / static_cast<float>(viz::lapCells) * twoPi - juce::MathConstants<float>::halfPi;
            g.setColour(palette::ink);
            g.drawLine(cx + std::cos(a) * (outer + 3.0f), cy + std::sin(a) * (outer + 3.0f), cx + std::cos(a) * (outer + 8.0f),
                       cy + std::sin(a) * (outer + 8.0f), 1.5f);
        }
    }

    // Bar divisions and the downbeat marker.
    g.setColour(palette::dim.withAlpha(0.7f));
    const int bars = juce::jmax(1, data.phraseBars);
    for (int i = 0; i < bars; ++i)
    {
        const float a = static_cast<float>(i) / static_cast<float>(bars) * twoPi - juce::MathConstants<float>::halfPi;
        const float r0 = outer - static_cast<float>(shown) * (track + gapPx) + gapPx - 2.0f;
        g.drawLine(cx + std::cos(a) * r0, cy + std::sin(a) * r0, cx + std::cos(a) * (outer + 2.0f), cy + std::sin(a) * (outer + 2.0f), i == 0 ? 1.5f : 0.7f);
    }
    // Now: a bead on the newest ring.
    if (data.timed)
    {
        const float a = static_cast<float>(data.nowFraction) * twoPi - juce::MathConstants<float>::halfPi;
        const float r = outer - track * 0.5f;
        g.setColour(palette::ink);
        g.fillEllipse(cx + std::cos(a) * r - 4.5f, cy + std::sin(a) * r - 4.5f, 9.0f, 9.0f);
    }
    g.setColour(palette::dim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(data.timed ? "lap " + juce::String(static_cast<long long>(data.lapIndex) + 1) : juce::String("stopped"), static_cast<int>(cx) - 40, static_cast<int>(cy) - 8, 80, 16, juce::Justification::centred);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("bar 1 at top, ticks = differs from previous lap", 16, getHeight() - 20, getWidth() - 32, 14, juce::Justification::centredLeft);
}
} // namespace groovemorph::ui::visual
