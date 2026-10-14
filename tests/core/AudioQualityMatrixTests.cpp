#include "core/dsp/RhythmEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

using namespace groovemorph::core;

namespace
{
int failures = 0;
void expect(std::string_view description, bool passed)
{
    if (!passed)
    {
        std::cerr << description << " failed\n";
        ++failures;
    }
}

VariationSettings busyScene()
{
    VariationSettings scene;
    for (auto& operation : scene.operations)
        operation = {true, 0.85f, 0.125f};
    return scene;
}

constexpr double pi = 3.14159265358979323846;

// This is a numerical regression matrix, not a perceptual listening verdict.
// Allocate and prepare outside processing. A silent right channel must not receive
// history from the non-silent left channel.
void exercise(double sampleRate, int maxBlockSize)
{
    const int samples = static_cast<int>(std::round(sampleRate * 0.25));
    const double samplesPerBeat = sampleRate / 2.0;
    std::vector<float> input(static_cast<std::size_t>(samples), 0.0f);
    for (int i = 0; i < samples; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        const float kick = (i % std::max(1, static_cast<int>(sampleRate / 20.0))) == 0 ? 0.25f : 0.0f;
        input[static_cast<std::size_t>(i)] =
            0.24f * static_cast<float>(std::sin(2.0 * pi * 193.0 * t))
            + 0.20f * static_cast<float>(std::sin(2.0 * pi * 3721.0 * t)) + kick;
    }

    const auto settings = busyScene();
    auto run = [&](RhythmEngine& engine, std::vector<float>& left,
                   std::vector<float>& right, float intensity)
    {
        for (int start = 0; start < samples; start += maxBlockSize)
        {
            const int n = std::min(maxBlockSize, samples - start);
            float* channels[] = {left.data() + start, right.data() + start};
            const RhythmTiming timing{samplesPerBeat, static_cast<double>(start) / samplesPerBeat, true};
            engine.process(channels, 2, n, settings, intensity, timing);
        }
    };

    RhythmEngine engine;
    engine.prepare(sampleRate, maxBlockSize, 2);
    expect("engine prepared for quality matrix", engine.isPrepared());
    std::vector<float> left = input, right(input.size(), 0.0f);
    run(engine, left, right, 0.0f);
    expect("dry output is bit exact across rates and blocks", left == input);
    expect("dry output keeps silent channel isolated", std::all_of(right.begin(), right.end(),
        [](float x) { return x == 0.0f; }));

    engine.reset();
    left = input;
    std::fill(right.begin(), right.end(), 0.0f);
    run(engine, left, right, 0.85f);
    const float inputPeak = *std::max_element(input.begin(), input.end(),
        [](float a, float b) { return std::abs(a) < std::abs(b); });
    const float peakBound = std::abs(inputPeak) + 0.002f;
    bool finite = true, bounded = true, isolated = true, altered = false;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        finite = finite && std::isfinite(left[i]) && std::isfinite(right[i]);
        bounded = bounded && std::abs(left[i]) <= peakBound;
        isolated = isolated && right[i] == 0.0f;
        altered = altered || std::abs(left[i] - input[i]) > 0.02f;
    }
    expect("effect output remains finite", finite);
    expect("effect does not exceed bounded input peak", bounded);
    expect("no inter-channel history leak", isolated);
    expect("effect meaningfully processes signal", altered);

    // Constant-time seek reset must never replay signal left behind in the
    // ring from a different timeline position.
    engine.resetHistory();
    std::vector<float> silence(static_cast<std::size_t>(samples), 0.0f);
    std::vector<float> silentRight = silence;
    run(engine, silence, silentRight, 0.85f);
    expect("after seek reset silence stays silent",
        std::all_of(silence.begin(), silence.end(), [](float x) { return x == 0.0f; }));
}
} // namespace

int main()
{
    for (const double sampleRate : {44100.0, 48000.0, 96000.0, 192000.0})
        for (const int blockSize : {16, 127, 1024})
            exercise(sampleRate, blockSize);
    if (failures == 0)
        std::cout << "AudioQualityMatrixTests passed (4 sample rates x 3 block sizes)\n";
    return failures == 0 ? 0 : 1;
}
