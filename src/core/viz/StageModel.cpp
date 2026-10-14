#include "core/viz/StageModel.h"

#include "core/viz/Ledger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace groovemorph::core::viz
{
std::string formatSlice(float beats)
{
    if (!std::isfinite(beats) || beats <= 0.0f)
        return "-";
    char buf[32];
    // Powers of two below one beat read best as fractions.
    const double inv = 1.0 / static_cast<double>(beats);
    const double rounded = std::round(inv);
    if (beats < 1.0f && rounded >= 2.0 && std::abs(inv - rounded) < 1.0e-3 && std::abs(std::log2(rounded) - std::round(std::log2(rounded))) < 1.0e-6)
    {
        std::snprintf(buf, sizeof buf, "1/%.0f beat", rounded);
        return buf;
    }
    if (std::abs(beats - std::round(beats)) < 1.0e-3f)
    {
        std::snprintf(buf, sizeof buf, "%.0f beat%s", static_cast<double>(std::round(beats)), std::round(beats) == 1.0f ? "" : "s");
        return buf;
    }
    std::snprintf(buf, sizeof buf, "%.2f beats", static_cast<double>(beats));
    return buf;
}

std::array<StageView, operationCount> buildStages(const VariationSettings& scene, const VariationSettings& sceneNow,
                                                  const std::array<std::uint8_t, operationCount>& locks,
                                                  const trace::TraceFrame* last)
{
    static constexpr const char* names[operationCount] = {"Repeat", "Reverse", "Gate", "HalfTime", "TapeStop"};
    static constexpr Effect effects[operationCount] = {Effect::Repeat, Effect::Reverse, Effect::Gate, Effect::HalfTime, Effect::TapeStop};
    const bool timed = last != nullptr && (last->flags & trace::TimingOk) != 0;
    std::array<StageView, operationCount> out{};
    for (std::size_t i = 0; i < operationCount; ++i)
    {
        StageView& v = out[i];
        v.effect = effects[i];
        v.name = names[i];
        v.timed = timed;
        v.locked = locks[i] != 0;
        v.amount = last != nullptr ? std::clamp(last->amount[i], 0.0f, 1.0f) : 0.0f;
        const OperationSettings& s = scene.operations[i];
        const OperationSettings& now = sceneNow.operations[i];
        if (!s.enabled)
        {
            v.state = StageState::Bypassed;
            continue;
        }
        v.state = (now.enabled && timed) ? StageState::Active : (timed ? StageState::HeldOff : StageState::Bypassed);
        if (timed && !now.enabled)
            v.state = StageState::HeldOff;
        v.text = formatSlice(now.enabled ? now.sliceBeats : s.sliceBeats);
        if (i == static_cast<std::size_t>(OperationType::Gate) && last != nullptr && v.state == StageState::Active)
            v.text += ", " + formatDb(static_cast<double>(1.0f - last->amount[i] + last->amount[i] * last->gateGain));
    }
    return out;
}

OrbitView buildOrbit(int activeScene, int queuedScene, std::int64_t samplesUntilSwitch, double samplesPerBeat,
                     int morphTarget, float morphAmount, int autoReturnTarget, bool previewing)
{
    OrbitView o;
    auto inRange = [](int i) { return i >= 0 && i < 4; };
    if (inRange(activeScene))
        o.nodes[static_cast<std::size_t>(activeScene)].sounding = true;
    if (inRange(queuedScene))
    {
        o.nodes[static_cast<std::size_t>(queuedScene)].queued = true;
        if (samplesUntilSwitch >= 0 && samplesPerBeat > 0.0)
            o.countdownBeats = static_cast<double>(samplesUntilSwitch) / samplesPerBeat;
    }
    if (inRange(morphTarget) && morphTarget != activeScene && morphAmount > 0.0f)
    {
        o.nodes[static_cast<std::size_t>(morphTarget)].morphTarget = true;
        o.morphTargetScene = morphTarget;
        o.morphAmount = std::clamp(morphAmount, 0.0f, 1.0f);
    }
    if (inRange(autoReturnTarget))
        o.nodes[static_cast<std::size_t>(autoReturnTarget)].returnTarget = true;
    o.previewing = previewing;
    return o;
}
} // namespace groovemorph::core::viz
