#include "core/dsp/RhythmEngine.h"

#include <algorithm>
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


    // ---- Audit regressions ----
    {
        // resetHistory() mid-stream must not add a step beyond the cross-fade (was ~0.45 FS).
        const OperationType history[] = {OperationType::Repeat, OperationType::ReverseSlice, OperationType::HalfTime,
                                         OperationType::TapeStop};
        for (auto type : history)
        {
            RhythmEngine engine;
            engine.prepare(48000.0, 480, 1);
            VariationSettings v;
            v[type] = {true, 1.0f, 0.25f};
            const double spb = 24000.0;
            float previous = 0.0f, worst = 0.0f;
            std::vector<float> block(480);
            for (int b = 0; b < 400; ++b)
            {
                if (b == 200)
                    engine.resetHistory();
                for (std::size_t i = 0; i < block.size(); ++i)
                    block[i] = 0.5f * std::sin(0.0130899f * static_cast<float>(b * 480 + static_cast<int>(i))); // 100 Hz
                RhythmTiming timing;
                timing.samplesPerBeat = spb;
                timing.beatAtBlockStart = b * 480.0 / spb;
                timing.isPlaying = true;
                float* ch[1] = {block.data()};
                engine.process(ch, 1, 480, v, 1.0f, timing);
                for (std::size_t i = 0; i < block.size(); ++i)
                {
                    // The single wet-to-dry step at the seek itself is inherent; everything after must be smooth.
                    if (b > 0 && !(b == 200 && i == 0))
                        worst = std::max(worst, std::abs(block[i] - previous));
                    previous = block[i];
                }
            }
            expect("history reset adds no click after the seek sample", worst < 0.06f);
        }
    }
    {
        // Non-finite input never poisons the history; all-operations-disabled is bit-exact dry.
        RhythmEngine engine;
        engine.prepare(48000.0, 480, 1);
        VariationSettings v;
        v[OperationType::Repeat] = {true, 1.0f, 0.25f};
        bool finite = true;
        for (int b = 0; b < 300; ++b)
        {
            std::vector<float> in(480, 0.2f);
            if (b == 100)
            {
                in[10] = std::nanf("");
                in[20] = INFINITY;
            }
            RhythmTiming timing;
            timing.samplesPerBeat = 24000.0;
            timing.beatAtBlockStart = b * 480.0 / 24000.0;
            timing.isPlaying = true;
            float* ch[1] = {in.data()};
            engine.process(ch, 1, 480, v, 1.0f, timing);
            for (float y : in)
                finite = finite && std::isfinite(y) && std::abs(y) < 1.0f;
        }
        expect("NaN/Inf input leaves no residue in later replays", finite);

        RhythmEngine plain;
        plain.prepare(48000.0, 480, 1);
        VariationSettings none;
        std::vector<float> tiny(480, 1.0e-35f), copy = tiny;
        RhythmTiming timing;
        timing.samplesPerBeat = 24000.0;
        timing.isPlaying = true;
        float* ch[1] = {tiny.data()};
        plain.process(ch, 1, 480, none, 1.0f, timing);
        expect("all operations disabled is bit-exact, even for tiny values", tiny == copy);
    }

    if (failures == 0)
        std::cout << "EnginePhaseTests passed\n";
    return failures == 0 ? 0 : 1;
}
