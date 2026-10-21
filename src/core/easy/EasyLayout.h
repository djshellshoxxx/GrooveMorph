#pragma once

#include <array>

// Pure layout of the N05 Easy view (no JUCE), so the geometry can be unit tested at the editor's
// minimum and maximum sizes. The Easy view fills the whole editor, so it scales exactly like it.
namespace groovemorph::core::easy
{
struct IntRect
{
    int x = 0, y = 0, w = 0, h = 0;
    [[nodiscard]] int right() const noexcept { return x + w; }
    [[nodiscard]] int bottom() const noexcept { return y + h; }
};

struct EasyLayout
{
    IntRect title, showGuide, openFull, timing;
    std::array<IntRect, 4> scenes;
    std::array<IntRect, 4> macroLabels;   // Intensity, Dry / wet, Morph, Chaos
    std::array<IntRect, 4> macroSliders;
    IntRect surprise, undo, panic, message;
    // Minimum height of a button this layout promises (accessibility / touch target).
    static constexpr int minButtonHeight = 44;
};

[[nodiscard]] EasyLayout computeEasyLayout(int width, int height) noexcept;
} // namespace groovemorph::core::easy
