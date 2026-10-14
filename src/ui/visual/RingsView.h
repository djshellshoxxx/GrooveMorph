#pragma once

#include "ui/visual/VisualStyle.h"

#include "core/viz/LapRecorder.h"

#include <array>

namespace groovemorph::ui::visual
{
// One ring per lap of the phrase (newest outermost). Angle = position in the phrase, so repeated material
// lines up radially and a change shows as a break in the pattern. Everything is measured, except the
// pencil arcs for the rest of the current lap (from the planner).
class RingsView final : public juce::Component
{
public:
    struct Data
    {
        std::array<groovemorph::core::viz::Lap, 4> laps{};   // newest first
        std::size_t lapCount = 0;
        groovemorph::core::viz::LapDiff diffNewest{};         // newest lap vs the one before (cells with data in both)
        std::array<std::uint8_t, groovemorph::core::viz::lapCells> planned{}; // Effect value per cell for the rest of the lap; 255 = none
        std::array<bool, groovemorph::core::viz::lapCells> allowed{};         // placement lets something act here
        bool placementActive = false;
        double nowFraction = 0.0;  // position in the phrase 0..1
        std::int64_t lapIndex = 0; // phrase number of the newest lap
        int phraseBars = 4;
        bool timed = false;
    };

    RingsView();
    void setData(Data newData);
    void paint(juce::Graphics&) override;

private:
    Data data;
};
} // namespace groovemorph::ui::visual
