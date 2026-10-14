#include "ui/visual/WeaveView.h"

#include <cmath>

namespace groovemorph::ui::visual
{
namespace viz = groovemorph::core::viz;

WeaveView::WeaveView()
{
    setOpaque(false);
    setTitle("Warp Weave");
    setDescription("Each thread ties a moment of what you hear, on the top row, to the moment of input it came from, on the bottom row.");
}

void WeaveView::setData(Data newData)
{
    data = std::move(newData);
    repaint();
}

juce::Rectangle<float> WeaveView::plotArea() const
{
    auto b = getLocalBounds().toFloat();
    return {b.getX() + 78.0f, b.getY() + 104.0f, juce::jmax(10.0f, b.getWidth() - 78.0f - 24.0f),
            juce::jmax(10.0f, b.getHeight() - 104.0f - 96.0f)};
}

void WeaveView::drawThread(juce::Graphics& g, const viz::Thread& t, const juce::Rectangle<float>& plot) const
{
    const float xo = xFor(t.xTop, plot);
    const float xs = xFor(t.xBottom, plot);
    const float yt = plot.getY();
    const float yb = plot.getBottom();
    if (t.noData)
    {
        g.setColour(palette::hatch.withAlpha(0.6f));
        g.drawLine(xo, yt, xo, yt + 7.0f, 1.0f);
        g.drawLine(xo, yb - 7.0f, xo, yb, 1.0f);
        return;
    }
    const float d = (yb - yt) * 0.55f;
    juce::Path p;
    p.startNewSubPath(xo, yt);
    p.cubicTo(xo, yt + d, xs, yb - d, xs, yb);
    const auto colour = (t.gateClosed ? effectColour(viz::Effect::Gate) : effectColour(t.primary)).withAlpha(t.opacity);
    g.setColour(colour);
    const float width = t.width;
    if (t.gateClosed || t.tentative)
    {
        const float dashes[] = {t.tentative ? 1.5f : 2.0f, 3.0f};
        juce::Path dashed;
        juce::PathStrokeType(width).createDashedStroke(dashed, p, dashes, 2);
        g.fillPath(dashed);
    }
    else
    {
        g.strokePath(p, juce::PathStrokeType(width));
    }
    if (t.clipped)
    {
        g.setColour(palette::ink.withAlpha(0.5f));
        g.fillRect(xs, yb - 3.0f, 3.0f, 3.0f);
    }
}

void WeaveView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(palette::panel);
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(palette::line);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);

    g.setColour(palette::ink);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText("WARP WEAVE", 16, 10, 220, 20, juce::Justification::centredLeft);
    g.setColour(palette::dim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("each thread ties what you hear (top) to the moment it was played (bottom)", 16, 30, getWidth() - 32, 16,
               juce::Justification::centredLeft);

    const auto plot = plotArea();
    const auto& p = data.params;
    const double scale = p.spanBeats + p.leadBeats;
    const double lo = p.viewStartBeat - p.leadBeats;
    auto xOfBeat = [&](double beat) { return plot.getX() + static_cast<float>((beat - lo) / scale) * plot.getWidth(); };

    // Row captions and beat rulers (the same scale on both rows).
    g.setColour(palette::dim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("HEARD NOW", 12, static_cast<int>(plot.getY()) - 8, 64, 16, juce::Justification::centredLeft);
    g.drawText("PLAYED", 12, static_cast<int>(plot.getBottom()) - 8, 64, 16, juce::Justification::centredLeft);
    const int bpb = juce::jmax(1, data.beatsPerBar);
    for (long b = static_cast<long>(std::ceil(lo)); static_cast<double>(b) <= p.viewStartBeat + p.spanBeats; ++b)
    {
        const float x = xOfBeat(static_cast<double>(b));
        const bool barLine = b % bpb == 0;
        g.setColour(palette::dim.withAlpha(barLine ? 0.9f : 0.45f));
        const float tick = barLine ? 9.0f : 4.0f;
        g.drawLine(x, plot.getY() - 6.0f - tick, x, plot.getY() - 6.0f, 1.0f);
        g.drawLine(x, plot.getBottom() + 6.0f, x, plot.getBottom() + 6.0f + tick, 1.0f);
        if (barLine)
        {
            const long bar = (b / bpb) % juce::jmax(1, data.phraseBars) + 1;
            g.setFont(juce::FontOptions(10.0f));
            g.drawText(juce::String(bar), static_cast<int>(x) + 3, static_cast<int>(plot.getY()) - 26, 24, 12,
                       juce::Justification::centredLeft);
        }
    }

    // Threads: planned first (underneath), then heard.
    for (const auto& t : data.planned)
        drawThread(g, t, plot);
    for (const auto& t : data.heard)
        drawThread(g, t, plot);

    // Ink front: heard = inked, to the right = pencil. Not a sweeping playhead line.
    const float front = xOfBeat(p.nowBeat);
    if (!data.heard.empty() || !data.planned.empty())
    {
        g.setColour(palette::panel.withAlpha(0.35f));
        g.fillRect(juce::Rectangle<float>(front, plot.getY() - 2.0f, plot.getRight() - front, plot.getHeight() + 4.0f));
        juce::Path tri;
        tri.addTriangle(front, plot.getY() - 6.0f, front - 6.0f, plot.getY() - 15.0f, front + 6.0f, plot.getY() - 15.0f);
        g.setColour(palette::ink);
        g.fillPath(tri);
        g.setColour(palette::dim);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText("planned", static_cast<int>(front) + 10, static_cast<int>(plot.getBottom()) - 18, 70, 12, juce::Justification::centredLeft);
        g.drawText("heard", static_cast<int>(front) - 80, static_cast<int>(plot.getBottom()) - 18, 70, 12, juce::Justification::centredRight);
    }

    // Legend: effect colour + the texture that carries the same meaning without colour.
    {
        struct Item { viz::Effect effect; const char* text; };
        const Item items[] = {{viz::Effect::Live, "straight = live"}, {viz::Effect::Repeat, "fan-in = repeat"},
                              {viz::Effect::Reverse, "twist = reverse"}, {viz::Effect::HalfTime, "spread = slower"},
                              {viz::Effect::TapeStop, "slows to a stop"}, {viz::Effect::Gate, "dotted = gate closed"}};
        g.setFont(juce::FontOptions(11.0f));
        float widths[std::size(items)];
        float total = 0.0f;
        for (std::size_t i = 0; i < std::size(items); ++i)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText(g.getCurrentFont(), items[i].text, 0.0f, 0.0f);
            widths[i] = glyphs.getBoundingBox(0, -1, true).getWidth() + 6.0f + 14.0f + 12.0f;
            total += widths[i];
        }
        // Wrap to a second row when the window is narrow instead of clipping.
        const bool wrap = total > plot.getWidth();
        float x = plot.getX();
        float y = static_cast<float>(getHeight()) - (wrap ? 50.0f : 30.0f);
        for (std::size_t i = 0; i < std::size(items); ++i)
        {
            if (x + widths[i] > plot.getRight() + 12.0f)
            {
                x = plot.getX();
                y += 16.0f;
            }
            g.setColour(effectColour(items[i].effect));
            g.fillRoundedRectangle(x, y + 3.0f, 10.0f, 10.0f, 2.0f);
            g.setColour(palette::dim);
            g.drawText(items[i].text, static_cast<int>(x) + 14, static_cast<int>(y), static_cast<int>(widths[i]) - 14, 16, juce::Justification::centredLeft);
            x += widths[i];
        }
    }

    // Honest state banner (idle, invalid timing, preview, no data).
    if (data.banner != viz::BannerKind::None && data.bannerText.isNotEmpty())
    {
        const auto box = juce::Rectangle<float>(plot.getX(), plot.getCentreY() - 14.0f, plot.getWidth(), 28.0f);
        g.setColour(palette::slate.withAlpha(0.85f));
        g.fillRoundedRectangle(box, 6.0f);
        g.setColour(data.banner == viz::BannerKind::Preview || data.banner == viz::BannerKind::Invalid ? palette::warning : palette::ink);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(data.bannerText, box, juce::Justification::centred);
    }
}
} // namespace groovemorph::ui::visual
