#include "ui/visual/StationsView.h"

#include "ui/visual/Glyphs.h"

#include <cmath>

namespace groovemorph::ui::visual
{
namespace viz = groovemorph::core::viz;

namespace
{
void drawPanel(juce::Graphics& g, juce::Rectangle<float> b, const juce::String& title, const juce::String& sub)
{
    g.setColour(palette::panel);
    g.fillRoundedRectangle(b, 10.0f);
    g.setColour(palette::line);
    g.drawRoundedRectangle(b.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(palette::ink);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText(title, static_cast<int>(b.getX()) + 16, static_cast<int>(b.getY()) + 10, static_cast<int>(b.getWidth()) - 32, 20, juce::Justification::centredLeft);
    g.setColour(palette::dim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(sub, static_cast<int>(b.getX()) + 16, static_cast<int>(b.getY()) + 30, static_cast<int>(b.getWidth()) - 32, 16, juce::Justification::centredLeft);
}
} // namespace

StationsView::StationsView()
{
    setTitle("Chain stations");
    setDescription("The five effects in signal order and what each is doing now.");
}

void StationsView::setStages(std::array<viz::StageView, groovemorph::core::operationCount> s, juce::String note)
{
    stages = std::move(s);
    footer = std::move(note);
    juce::String text;
    for (const auto& st : stages)
        text << st.name << ": " << (st.state == viz::StageState::Active ? "active" : st.state == viz::StageState::HeldOff ? "held off" : "bypassed")
             << (st.text.empty() ? "" : ", " + juce::String(st.text)) << ". ";
    setDescription(text);
    repaint();
}

void StationsView::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    drawPanel(g, b, "CHAIN STATIONS", "the real signal path, in order: what each stage is doing right now");
    const float top = 56.0f, bottom = b.getHeight() - 30.0f;
    const float gap = 10.0f;
    const float cell = (b.getWidth() - 32.0f - gap * 4.0f) / 5.0f;
    // The pipe the audio flows through.
    g.setColour(palette::line);
    g.drawLine(12.0f, (top + bottom) * 0.5f, b.getWidth() - 12.0f, (top + bottom) * 0.5f, 8.0f);
    g.setColour(palette::dim);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("IN", 4, static_cast<int>(top) - 14, 24, 12, juce::Justification::centredLeft);
    g.drawText("OUT", static_cast<int>(b.getWidth()) - 30, static_cast<int>(top) - 14, 28, 12, juce::Justification::centredRight);

    for (std::size_t i = 0; i < stages.size(); ++i)
    {
        const auto& st = stages[i];
        const bool active = st.state == viz::StageState::Active;
        const auto colour = active ? effectColour(st.effect) : palette::dim;
        auto box = juce::Rectangle<float>(16.0f + static_cast<float>(i) * (cell + gap), top, cell, bottom - top);
        const auto outline = box;
        g.setColour(palette::slate);
        g.fillRoundedRectangle(outline, 10.0f);
        g.setColour(active ? colour : palette::line);
        g.drawRoundedRectangle(outline.reduced(0.5f), 10.0f, active ? 2.0f : 1.0f);

        g.setColour(active ? palette::ink : palette::dim);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(juce::String(st.name), box.removeFromTop(22.0f).toNearestInt(), juce::Justification::centred);
        auto glyphArea = box.removeFromTop(juce::jmin(46.0f, box.getHeight() * 0.38f));
        drawGlyph(g, st.effect, glyphArea.reduced(6.0f, 2.0f), colour);
        // Depth bar: how much of the stage is applied (measured, smoothed).
        auto bar = box.removeFromTop(14.0f).reduced(10.0f, 3.0f);
        g.setColour(palette::line);
        g.fillRoundedRectangle(bar, 4.0f);
        if (active)
        {
            g.setColour(colour);
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * st.amount), 4.0f);
        }
        g.setColour(active ? palette::ink : palette::dim);
        g.setFont(juce::FontOptions(11.0f));
        const juce::String state = !st.timed && st.state != viz::StageState::Bypassed ? "stopped"
                                   : st.state == viz::StageState::Active  ? "active"
                                   : st.state == viz::StageState::HeldOff ? "held off"
                                                                          : "bypassed";
        g.drawFittedText(juce::String(st.text), box.removeFromTop(16.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        g.setColour(palette::dim);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(state, box.removeFromTop(14.0f).toNearestInt(), juce::Justification::centred);
        if (st.locked)
        {
            g.setColour(palette::warning);
            g.drawText("locked", box.removeFromTop(14.0f).toNearestInt(), juce::Justification::centred);
        }
    }
    g.setColour(palette::dim);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText(footer, 16, static_cast<int>(b.getHeight()) - 24, static_cast<int>(b.getWidth()) - 32, 16, juce::Justification::centredLeft);
}

SceneOrbitView::SceneOrbitView()
{
    setTitle("Scene orbit");
    setDescription("Scenes A to D: which is sounding, which is queued, morph and return.");
}

void SceneOrbitView::setOrbit(viz::OrbitView o)
{
    orbit = o;
    juce::String text;
    for (int i = 0; i < 4; ++i)
    {
        const auto& n = orbit.nodes[static_cast<std::size_t>(i)];
        text << juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)) << ": "
             << (n.sounding ? "sounding" : n.queued ? "queued" : "idle") << (n.returnTarget ? ", return target" : "") << ". ";
    }
    setDescription(text);
    repaint();
}

void SceneOrbitView::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    drawPanel(g, b, "SCENE ORBIT", "where you are, where you are going, and the way back");
    const float r = juce::jlimit(16.0f, 28.0f, juce::jmin(b.getWidth() / 8.0f, (b.getHeight() - 70.0f) / 5.0f));
    const float x0 = b.getX() + b.getWidth() * 0.27f, x1 = b.getX() + b.getWidth() * 0.73f;
    const float y0 = b.getY() + 56.0f + r + 6.0f, y1 = b.getBottom() - 34.0f - r;
    const juce::Point<float> pos[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    int sounding = 0;
    for (int i = 0; i < 4; ++i)
        if (orbit.nodes[static_cast<std::size_t>(i)].sounding)
            sounding = i;

    // Morph filament from the sounding scene to its target, with a bead at the blend position.
    if (orbit.morphTargetScene >= 0)
    {
        const auto a = pos[sounding], t = pos[orbit.morphTargetScene];
        g.setColour(palette::line);
        g.drawLine(a.x, a.y, t.x, t.y, 5.0f);
        const auto m = a + (t - a) * orbit.morphAmount;
        g.setColour(palette::ink);
        g.drawLine(a.x, a.y, m.x, m.y, 5.0f);
        g.setColour(palette::slate);
        g.fillEllipse(m.x - 6.0f, m.y - 6.0f, 12.0f, 12.0f);
        g.setColour(palette::ink);
        g.drawEllipse(m.x - 6.0f, m.y - 6.0f, 12.0f, 12.0f, 2.0f);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(juce::String(static_cast<int>(std::lround(orbit.morphAmount * 100.0f))) + " % morph", static_cast<int>(m.x) - 40, static_cast<int>(m.y) - 24, 80, 12, juce::Justification::centred);
    }
    // Dashed way back to the auto-return target.
    for (int i = 0; i < 4; ++i)
    {
        if (!orbit.nodes[static_cast<std::size_t>(i)].returnTarget || i == sounding)
            continue;
        juce::Path p;
        p.startNewSubPath(pos[sounding]);
        const auto mid = (pos[sounding] + pos[i]) * 0.5f + juce::Point<float>(0.0f, 14.0f);
        p.quadraticTo(mid, pos[i]);
        juce::Path dashed;
        const float dashes[] = {4.0f, 4.0f};
        juce::PathStrokeType(1.5f).createDashedStroke(dashed, p, dashes, 2);
        g.setColour(palette::dim);
        g.fillPath(dashed);
    }
    for (int i = 0; i < 4; ++i)
    {
        const auto& n = orbit.nodes[static_cast<std::size_t>(i)];
        const auto c = pos[i];
        g.setColour(n.sounding ? palette::ink : palette::slate);
        g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour(n.sounding || n.queued ? palette::ink : palette::line);
        g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 2.0f);
        g.setColour(n.sounding ? palette::slate : palette::ink);
        g.setFont(juce::FontOptions(r * 0.9f, juce::Font::bold));
        g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)), static_cast<int>(c.x - r), static_cast<int>(c.y - r), static_cast<int>(r * 2.0f), static_cast<int>(r * 2.0f), juce::Justification::centred);
        if (n.queued)
        {
            g.setColour(effectColour(viz::Effect::Reverse));
            g.drawEllipse(c.x - r - 6.0f, c.y - r - 6.0f, (r + 6.0f) * 2.0f, (r + 6.0f) * 2.0f, 3.0f);
            g.setFont(juce::FontOptions(10.0f));
            const juce::String t = orbit.countdownBeats >= 0.0 ? "queued, " + juce::String(orbit.countdownBeats, 1) + " beats" : "queued";
            g.drawText(t, static_cast<int>(c.x) - 60, static_cast<int>(c.y + r + 8.0f), 120, 12, juce::Justification::centred);
        }
        if (n.returnTarget && !n.sounding)
        {
            g.setColour(palette::dim);
            g.setFont(juce::FontOptions(10.0f));
            g.drawText("return", static_cast<int>(c.x) - 30, static_cast<int>(c.y - r - 18.0f), 60, 12, juce::Justification::centred);
        }
    }
    g.setColour(palette::dim);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText(orbit.previewing ? "PREVIEW: sounding scene is not committed" : "filled = sounding, ring = queued, bead = morph",
               16, static_cast<int>(b.getHeight()) - 24, static_cast<int>(b.getWidth()) - 32, 16, juce::Justification::centredLeft);
}
} // namespace groovemorph::ui::visual
