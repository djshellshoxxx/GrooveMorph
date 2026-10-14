#include "ui/visual/Glyphs.h"

namespace groovemorph::ui::visual
{
namespace viz = groovemorph::core::viz;

void drawGlyph(juce::Graphics& g, viz::Effect effect, juce::Rectangle<float> a, juce::Colour colour, float lw)
{
    g.setColour(colour);
    const float cx = a.getCentreX(), cy = a.getCentreY(), w = a.getWidth(), h = a.getHeight();
    switch (effect)
    {
        case viz::Effect::Repeat: // three identical frames: the same slice again and again
        {
            const float fw = w * 0.22f, gap = w * 0.12f;
            const float x0 = cx - (3.0f * fw + 2.0f * gap) * 0.5f;
            for (int k = 0; k < 3; ++k)
                g.drawRoundedRectangle(x0 + static_cast<float>(k) * (fw + gap), cy - h * 0.3f, fw, h * 0.6f, 2.0f, lw * 0.8f);
            break;
        }
        case viz::Effect::Reverse: // double chevron pointing back in time
        {
            juce::Path p;
            for (int k = 0; k < 2; ++k)
            {
                const float x = cx + w * 0.12f - static_cast<float>(k) * w * 0.26f;
                p.startNewSubPath(x + w * 0.16f, cy - h * 0.3f);
                p.lineTo(x - w * 0.02f, cy);
                p.lineTo(x + w * 0.16f, cy + h * 0.3f);
            }
            g.strokePath(p, juce::PathStrokeType(lw, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
            break;
        }
        case viz::Effect::Gate: // square wave: open, closed, open
        {
            juce::Path p;
            const float x0 = cx - w * 0.36f, step = w * 0.18f, top = cy - h * 0.3f, bot = cy + h * 0.3f;
            p.startNewSubPath(x0, bot);
            p.lineTo(x0, top);
            p.lineTo(x0 + step * 2.0f, top);
            p.lineTo(x0 + step * 2.0f, bot);
            p.lineTo(x0 + step * 3.0f, bot);
            p.lineTo(x0 + step * 3.0f, top);
            p.lineTo(x0 + step * 4.0f, top);
            p.lineTo(x0 + step * 4.0f, bot);
            g.strokePath(p, juce::PathStrokeType(lw, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
            break;
        }
        case viz::Effect::HalfTime: // a stretched wave: the same shape at half speed
        {
            juce::Path fast, slow;
            const float x0 = cx - w * 0.38f, x1 = cx + w * 0.38f;
            fast.startNewSubPath(x0, cy);
            for (int k = 0; k <= 40; ++k)
            {
                const float t = static_cast<float>(k) / 40.0f;
                fast.lineTo(x0 + t * (x1 - x0), cy - std::sin(t * juce::MathConstants<float>::twoPi * 2.0f) * h * 0.22f);
            }
            slow.startNewSubPath(x0, cy);
            for (int k = 0; k <= 40; ++k)
            {
                const float t = static_cast<float>(k) / 40.0f;
                slow.lineTo(x0 + t * (x1 - x0), cy - std::sin(t * juce::MathConstants<float>::twoPi) * h * 0.3f);
            }
            g.setColour(colour.withAlpha(0.35f));
            g.strokePath(fast, juce::PathStrokeType(lw * 0.7f));
            g.setColour(colour);
            g.strokePath(slow, juce::PathStrokeType(lw));
            break;
        }
        case viz::Effect::TapeStop: // dots whose spacing grows until it stops
        {
            float x = cx - w * 0.38f;
            const float gaps[] = {0.05f, 0.08f, 0.12f, 0.19f};
            for (int k = 0; k < 5; ++k)
            {
                g.fillEllipse(x - lw, cy - lw, lw * 2.0f, lw * 2.0f);
                if (k < 4)
                    x += w * (gaps[k] + 0.03f);
            }
            break;
        }
        case viz::Effect::Live:
            g.drawLine(cx - w * 0.3f, cy, cx + w * 0.3f, cy, lw);
            break;
    }
}
} // namespace groovemorph::ui::visual
