#pragma once

#include "core/OperationParams.h"
#include "core/trace/TraceFrame.h"
#include "core/viz/WeaveLayout.h"

#include <array>
#include <cstdint>
#include <string>

// Display models for the Chain Stations and Scene Orbit views (docs/visual-feedback/SPEC.md section 8).
// Pure: the views only draw what these return.
namespace groovemorph::core::viz
{
// Slice length in meter beats as short text: 0.25 -> "1/4 beat", 1 -> "1 beat", 0.0625 -> "1/16 beat".
[[nodiscard]] std::string formatSlice(float beats);

enum class StageState : std::uint8_t
{
    Bypassed, // not enabled in the scene
    HeldOff,  // enabled in the scene but gated off right now (placement window, event lane or activity limit)
    Active,   // doing something to the sound now
};

struct StageView
{
    Effect effect = Effect::Live;
    std::string name;
    StageState state = StageState::Bypassed;
    float amount = 0.0f;   // measured applied depth 0..1 (0 without frames)
    std::string text;      // parameters, e.g. "1/8 beat" or "1/4 beat, -14.0 dB"
    bool locked = false;   // any permission lock on this effect
    bool timed = false;    // false when the transport is stopped or timing invalid: nothing is being processed
};

// scene: the committed scene settings; sceneNow: effective settings right now (placement/limits/morph applied);
// operationLocks: SceneControl permission bits per effect; last: newest trace frame or nullptr.
[[nodiscard]] std::array<StageView, operationCount> buildStages(const VariationSettings& scene,
                                                                const VariationSettings& sceneNow,
                                                                const std::array<std::uint8_t, operationCount>& operationLocks,
                                                                const trace::TraceFrame* last);

struct OrbitNode
{
    bool sounding = false;
    bool queued = false;
    bool morphTarget = false;
    bool returnTarget = false;
};

struct OrbitView
{
    std::array<OrbitNode, 4> nodes{};
    float morphAmount = 0.0f;         // 0..1 towards the morph target
    int morphTargetScene = -1;
    double countdownBeats = -1.0;     // beats until the queued switch; -1 unknown
    bool previewing = false;
};

[[nodiscard]] OrbitView buildOrbit(int activeScene, int queuedScene, std::int64_t samplesUntilSwitch, double samplesPerBeat,
                                   int morphTarget, float morphAmount, int autoReturnTarget, bool previewing);
} // namespace groovemorph::core::viz
