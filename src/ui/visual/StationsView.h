#pragma once

#include "ui/visual/VisualStyle.h"

#include "core/viz/StageModel.h"

#include <array>

namespace groovemorph::ui::visual
{
// The real signal path, left to right, with what each stage is doing right now.
class StationsView final : public juce::Component
{
public:
    StationsView();
    void setStages(std::array<groovemorph::core::viz::StageView, groovemorph::core::operationCount> newStages, juce::String note);
    void paint(juce::Graphics&) override;

private:
    std::array<groovemorph::core::viz::StageView, groovemorph::core::operationCount> stages{};
    juce::String footer;
};

// Scenes A to D: sounding, queued (with countdown), morph and the way back.
class SceneOrbitView final : public juce::Component
{
public:
    SceneOrbitView();
    void setOrbit(groovemorph::core::viz::OrbitView newOrbit);
    void paint(juce::Graphics&) override;

private:
    groovemorph::core::viz::OrbitView orbit;
};
} // namespace groovemorph::ui::visual
