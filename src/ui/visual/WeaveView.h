#pragma once

#include "ui/visual/VisualStyle.h"

#include "core/viz/Ledger.h"
#include "core/viz/WeaveLayout.h"

#include <vector>

namespace groovemorph::ui::visual
{
// The Warp Weave: every thread ties a moment of what is heard (top) to the moment it was played (bottom).
// Draws only geometry produced by core/viz; planned threads are pencil, heard threads are ink.
class WeaveView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Data
    {
        std::vector<groovemorph::core::viz::Thread> heard;
        std::vector<groovemorph::core::viz::Thread> planned;
        groovemorph::core::viz::WeaveParams params;
        int beatsPerBar = 4;
        int phraseBars = 4;
        groovemorph::core::viz::BannerKind banner = groovemorph::core::viz::BannerKind::NoData;
        juce::String bannerText;
    };

    WeaveView();
    void setData(Data newData);
    void paint(juce::Graphics&) override;

private:
    Data data;
    juce::Rectangle<float> plotArea() const;
    float xFor(float normalised, const juce::Rectangle<float>& plot) const { return plot.getX() + normalised * plot.getWidth(); }
    void drawThread(juce::Graphics&, const groovemorph::core::viz::Thread&, const juce::Rectangle<float>& plot) const;
};
} // namespace groovemorph::ui::visual
