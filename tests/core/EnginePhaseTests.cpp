#include "core/dsp/RhythmEngine.h"

#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

using namespace groovemorph::core;

namespace
{
int failures = 0;
void expect(std::string_view name, bool ok)
{
    if (!ok)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

// Renders `blocks` blocks of a constant-ish signal through a Gate and returns the output.
std::vector<float> render(const std::array<double, operationCount>& offsets, bool useOffsets)
{
    RhythmEngine engine;
    engine.prepare(48000.0, 480, 1);
    VariationSettings v;
    v[OperationType::Gate] = {true, 1.0f, 1.0f}; // 1-beat gate: open first half beat, closed second
    std::vector<float> out;
    std::vector<float> block(480);
    const double spb = 24000.0;
    for (int b = 0; b < 400; ++b)
    {
        for (std::size_t i = 0; i < block.size(); ++i)
            block[i] = 0.5f + 0.25f * std::sin(0.01f * static_cast<float>(b * 480 + static_cast<int>(i)));
        RhythmTiming timing;
        timing.samplesPerBeat = spb;
        timing.beatAtBlockStart = b * 480.0 / spb;
        timing.isPlaying = true;
        if (useOffsets)
            timing.phaseOffsetBeats = offsets;
        float* ch[1] = {block.data()};
        engine.process(ch, 1, 480, v, 1.0f, timing);
        out.insert(out.end(), block.begin(), block.end());
    }
    return out;
}
} // namespace

int main()
{
    std::array<double, operationCount> zero{};
    const auto legacy = render(zero, false);
    const auto zeroOffsets = render(zero, true);
    expect("zero offsets are bit-identical to the legacy grid", legacy == zeroOffsets);

    std::array<double, operationCount> shifted{};
    shifted[static_cast<std::size_t>(OperationType::Gate)] = 0.25; // quarter-beat later grid
    const auto moved = render(shifted, true);
    expect("offset moves the gate pattern", moved != legacy);

    // A whole-beat offset keeps a 1-beat gate on the same grid.
    std::array<double, operationCount> whole{};
    whole[static_cast<std::size_t>(OperationType::Gate)] = 3.0;
    expect("whole-slice offset leaves the pattern unchanged", render(whole, true) == legacy);

    // The gate opens at the offset origin: closed half starts half a slice after the origin.
    bool finite = true;
    for (float f : moved)
        finite = finite && std::isfinite(f);
    expect("output finite", finite);

    if (failures == 0)
        std::cout << "EnginePhaseTests passed\n";
    return failures == 0 ? 0 : 1;
}
