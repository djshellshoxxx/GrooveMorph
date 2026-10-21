#include "core/easy/EasyLayout.h"

#include <algorithm>

namespace groovemorph::core::easy
{
EasyLayout computeEasyLayout(int width, int height) noexcept
{
    EasyLayout layout;
    width = std::max(width, 320); // below this the view cannot be laid out; rectangles may exceed the bounds
    height = std::max(height, 240);
    const int margin = std::clamp(std::min(width, height) / 30, 8, 28);
    const int gap = std::clamp(margin / 2, 6, 14);
    const int contentW = std::max(1, width - 2 * margin);

    const int headerH = std::clamp(height / 14, 36, 56);
    const int timingH = 28;
    const int macroRowH = std::clamp(height / 14, 40, 56);
    const int actionH = std::clamp(height / 9, EasyLayout::minButtonHeight + 4, 80);
    const int messageH = 28;
    const int fixed = headerH + timingH + 2 * macroRowH + gap + actionH + messageH + 5 * gap;
    const int scenesH = std::max(72, height - 2 * margin - fixed);

    int y = margin;
    // Header: title on the left, two buttons on the right.
    const int buttonW = std::clamp(contentW / 6, 110, 180);
    layout.openFull = {margin + contentW - buttonW, y, buttonW, headerH};
    layout.showGuide = {layout.openFull.x - gap - buttonW, y, buttonW, headerH};
    layout.title = {margin, y, std::max(1, layout.showGuide.x - gap - margin), headerH};
    y += headerH + gap;

    layout.timing = {margin, y, contentW, timingH};
    y += timingH + gap;

    const int sceneW = (contentW - 3 * gap) / 4;
    for (int i = 0; i < 4; ++i)
        layout.scenes[static_cast<std::size_t>(i)] = {margin + i * (sceneW + gap), y, sceneW, scenesH};
    y += scenesH + gap;

    const int columnW = (contentW - gap) / 2;
    const int labelW = std::clamp(columnW / 4, 90, 150);
    for (int i = 0; i < 4; ++i)
    {
        const int column = i % 2, row = i / 2;
        const int cx = margin + column * (columnW + gap);
        const int cy = y + row * (macroRowH + gap);
        layout.macroLabels[static_cast<std::size_t>(i)] = {cx, cy, labelW, macroRowH};
        layout.macroSliders[static_cast<std::size_t>(i)] = {cx + labelW, cy, columnW - labelW, macroRowH};
    }
    y += 2 * macroRowH + gap + gap;

    const int smallW = (contentW - 2 * gap) / 4;
    const int bigW = contentW - 2 * gap - 2 * smallW;
    layout.surprise = {margin, y, bigW, actionH};
    layout.undo = {margin + bigW + gap, y, smallW, actionH};
    layout.panic = {margin + bigW + gap + smallW + gap, y, smallW, actionH};
    y += actionH + gap;

    layout.message = {margin, y, contentW, messageH};
    return layout;
}
} // namespace groovemorph::core::easy
