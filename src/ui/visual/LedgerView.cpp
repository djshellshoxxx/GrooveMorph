#include "ui/visual/LedgerView.h"

#include <algorithm>

namespace groovemorph::ui::visual
{
LedgerView::LedgerView()
{
    setTooltip("Change Ledger: the numbers behind the pictures (how far back you are hearing, reading speed, gate, levels, history, timing, position, scene). Copy as text (C) copies them all.");
    setOpaque(false);
    setTitle("Change ledger");
    setDescription("Numbers describing what the plugin is doing to the sound right now.");
}

void LedgerView::setLedger(groovemorph::core::viz::Ledger newLedger)
{
    ledger = std::move(newLedger);
    setDescriptionThrottled(*this, descriptionThrottle, asText());
    repaint();
}

void LedgerView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(palette::panel);
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(palette::line);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);

    g.setColour(palette::ink);
    g.setFont(theme::font(theme::fontValue, true));
    g.drawText("CHANGE LEDGER", 16, 10, getWidth() - 32, 20, juce::Justification::centredLeft);
    g.setColour(palette::dim);
    g.setFont(theme::font(theme::fontCaption));
    g.drawText("numbers behind the picture", 16, 30, getWidth() - 32, 16, juce::Justification::centredLeft);

    float y = 56.0f;
    if (!ledger.bannerText.empty())
    {
        const auto box = juce::Rectangle<float>(12.0f, y, static_cast<float>(getWidth()) - 24.0f, 34.0f);
        g.setColour(palette::slate);
        g.fillRoundedRectangle(box, 6.0f);
        g.setColour(ledger.banner == groovemorph::core::viz::BannerKind::Preview
                            || ledger.banner == groovemorph::core::viz::BannerKind::Invalid
                        ? palette::warning
                        : palette::ink);
        g.setFont(theme::font(theme::fontLabel, true));
        g.drawFittedText(juce::String(ledger.bannerText), box.reduced(8.0f, 2.0f).toNearestInt(), juce::Justification::centredLeft, 2);
        y += 42.0f;
    }
    const float rowHeight = juce::jmin(40.0f, (static_cast<float>(getHeight()) - y - 8.0f) / static_cast<float>(std::max<std::size_t>(1, ledger.rows.size())));
    for (const auto& row : ledger.rows)
    {
        g.setColour(palette::dim);
        g.setFont(theme::font(theme::fontCaption));
        g.drawText(juce::String(row.label), 16, static_cast<int>(y), getWidth() - 32, 14, juce::Justification::centredLeft);
        g.setColour(palette::ink);
        g.setFont(theme::font(theme::fontValue, true));
        g.drawText(juce::String(row.value), 16, static_cast<int>(y) + 13, getWidth() - 32, 20, juce::Justification::centredLeft);
        if (!row.note.empty())
        {
            g.setColour(palette::dim);
            g.setFont(theme::font(theme::fontCaption));
            g.drawText(juce::String(row.note), 16, static_cast<int>(y) + 13, getWidth() - 32, 20, juce::Justification::centredRight);
        }
        y += rowHeight;
    }
}
} // namespace groovemorph::ui::visual
