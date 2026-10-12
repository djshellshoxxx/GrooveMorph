#include "core/dsp/RhythmEngine.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

using namespace groovemorph::core;

namespace
{
int failures = 0;

void expect(std::string_view name, bool condition)
{
    if (!condition)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

constexpr double sampleRate = 48000.0;
constexpr double samplesPerBeat = 4800.0; // 600 BPM keeps fixtures short
constexpr int block = 128;
constexpr double pi = 3.14159265358979323846;

std::vector<float> sine(std::size_t n, double hz, float amp = 0.5f)
{
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = amp * static_cast<float>(std::sin(2.0 * pi * hz * static_cast<double>(i) / sampleRate));
    return v;
}

// Runs a stereo copy of `input` through the engine in fixed blocks. The variation for each block
// is chosen by `pick(blockStartSample)`.
template <typename Pick>
std::vector<float> run(RhythmEngine& engine, const std::vector<float>& input, float intensity, Pick pick,
                       bool playing = true)
{
    std::vector<float> left = input;
    std::vector<float> right = input;
    for (std::size_t start = 0; start < input.size(); start += block)
    {
        const int n = static_cast<int>(std::min<std::size_t>(block, input.size() - start));
        float* chans[] = {left.data() + start, right.data() + start};
        const RhythmTiming timing{samplesPerBeat, static_cast<double>(start) / samplesPerBeat, playing};
        engine.process(chans, 2, n, pick(start), intensity, timing);
    }
    for (std::size_t i = 0; i < left.size(); ++i)
        if (left[i] != right[i])
            return {};
    return left;
}

VariationSettings only(OperationType type, float sliceBeats, float depth = 1.0f)
{
    VariationSettings v;
    v[type].enabled = true;
    v[type].depth = depth;
    v[type].sliceBeats = sliceBeats;
    return v;
}

VariationSettings everything()
{
    VariationSettings v;
    for (auto& op : v.operations)
    {
        op.enabled = true;
        op.depth = 0.7f;
        op.sliceBeats = 0.5f;
    }
    return v;
}

float maxStep(const std::vector<float>& v, std::size_t from = 1)
{
    float m = 0.0f;
    for (std::size_t i = std::max<std::size_t>(from, 1); i < v.size(); ++i)
        m = std::max(m, std::abs(v[i] - v[i - 1]));
    return m;
}
} // namespace

int main()
{
    RhythmEngine engine;
    engine.prepare(sampleRate, block, 2);
    expect("prepared", engine.isPrepared());
    const int fade = engine.crossfadeSamples();
    expect("crossfade 3 ms", fade == 144);

    const auto input = sine(48000, 220.0);

    // GM-FR-005: intensity 0 is a bit-exact null even with every operation enabled.
    {
        engine.reset();
        const auto out = run(engine, input, 0.0f, [](std::size_t) { return everything(); });
        expect("null test same size", out.size() == input.size());
        expect("null test exact", out == input);
    }

    // Unprepared engine passes audio through untouched.
    {
        RhythmEngine idle;
        const auto out = run(idle, input, 1.0f, [](std::size_t) { return everything(); });
        expect("unprepared passthrough", out == input);
    }

    // GM-QR-007: deterministic output; reset() clears state.
    {
        engine.reset();
        const auto a = run(engine, input, 1.0f, [](std::size_t) { return everything(); });
        engine.reset();
        const auto b = run(engine, input, 1.0f, [](std::size_t) { return everything(); });
        expect("deterministic after reset", !a.empty() && a == b);
        bool clean = true;
        for (float s : a)
            clean = clean && std::isfinite(s) && (s == 0.0f || std::fpclassify(s) == FP_NORMAL);
        expect("no NaN/denormals", clean);
    }

    // Repeat: slices 1..3 of each cycle replay slice 0.
    {
        engine.reset();
        const float sliceBeats = 0.25f;
        const auto len = static_cast<std::size_t>(sliceBeats * samplesPerBeat); // 1200
        const auto out = run(engine, input, 1.0f, [&](std::size_t) { return only(OperationType::Repeat, sliceBeats); });
        bool ok = !out.empty();
        const std::size_t cycle = 4 * len * 2; // second cycle
        for (std::size_t j = 1; j < 4 && ok; ++j)
            for (std::size_t p = static_cast<std::size_t>(fade) + 1; p < len; ++p)
                ok = ok && std::abs(out[cycle + j * len + p] - input[cycle + p]) < 1.0e-6f;
        expect("repeat loops first slice", ok);
        bool live = true;
        for (std::size_t p = static_cast<std::size_t>(fade) + 1; p < len; ++p)
            live = live && out[cycle + p] == input[cycle + p];
        expect("repeat first slice live", live);
    }

    // ReverseSlice on a ramp yields the time-reversed previous slice.
    {
        engine.reset();
        std::vector<float> ramp(24000);
        for (std::size_t i = 0; i < ramp.size(); ++i)
            ramp[i] = static_cast<float>(i);
        const float sliceBeats = 0.25f;
        const auto len = static_cast<std::size_t>(sliceBeats * samplesPerBeat);
        const auto out = run(engine, ramp, 1.0f, [&](std::size_t) { return only(OperationType::ReverseSlice, sliceBeats); });
        bool ok = !out.empty();
        for (std::size_t k = 1; k < 10 && ok; ++k)
        {
            const std::size_t s = k * len;
            for (std::size_t p = static_cast<std::size_t>(fade) + 1; p < len; ++p)
                ok = ok && std::abs(out[s + p] - static_cast<float>(s - 1 - p)) < 1.0e-3f;
        }
        expect("reverse is time-reversed slice", ok);
    }

    // Gate zeroes the off half of each slice (after the ramp).
    {
        engine.reset();
        const float sliceBeats = 0.5f;
        const auto len = static_cast<std::size_t>(sliceBeats * samplesPerBeat);
        const auto ramp = static_cast<std::size_t>(engine.gateRampSamples());
        const auto out = run(engine, input, 1.0f, [&](std::size_t) { return only(OperationType::Gate, sliceBeats); });
        bool off = !out.empty();
        bool on = !out.empty();
        for (std::size_t k = 0; k < 8; ++k)
        {
            const std::size_t s = k * len;
            for (std::size_t p = len / 2 + ramp; p < len; ++p)
                off = off && out[s + p] == 0.0f;
            for (std::size_t p = ramp; p < len / 2; ++p)
                on = on && out[s + p] == input[s + p];
        }
        expect("gate off phase is silent", off);
        expect("gate on phase is dry", on);
        expect("gate ramps are smooth", maxStep(out) < 0.3f);
    }

    // GM-QR-004: toggling each history op on/off (and switching variations) on a sine
    // causes no step larger than the sine's own slope plus a small margin.
    {
        const auto tone = sine(48000, 440.0, 0.5f);
        const float natural = maxStep(tone);
        const float limit = natural * 1.6f;
        const OperationType types[] = {OperationType::Repeat, OperationType::ReverseSlice,
                                       OperationType::Gate, OperationType::HalfTime,
                                       OperationType::TapeStop};
        for (auto type : types)
        {
            engine.reset();
            const auto out = run(engine, tone, 1.0f, [&](std::size_t start) {
                const bool enabled = (start / 3000) % 2 == 1;
                auto v = only(type, (start / 12000) % 2 == 0 ? 0.25f : 0.5f);
                v[type].enabled = enabled;
                return v;
            });
            const float step = maxStep(out);
            if (step > limit) std::cerr << "op " << static_cast<int>(type) << " step " << step << " limit " << limit << "\n";
            if (type != OperationType::Gate)
                expect("toggle click-free", !out.empty() && step <= limit);
            else
                expect("gate toggle bounded", !out.empty() && step <= natural + 0.5f / static_cast<float>(engine.gateRampSamples()) * 1.5f);
        }

        // Variation change between two full variations, intensity automation and transport stop.
        engine.reset();
        const auto out = run(engine, tone, 1.0f, [&](std::size_t start) {
            if ((start / 4096) % 2 == 0)
                return everything();
            VariationSettings v = everything();
            v[OperationType::Gate].enabled = false;
            for (auto& op : v.operations)
                op.sliceBeats = 0.125f;
            return v;
        });
        expect("variation switch bounded", !out.empty() && maxStep(out) < 0.5f);
    }

    // Not playing: operations ramp to bypass and output becomes dry.
    {
        engine.reset();
        const auto out = run(engine, input, 1.0f, [](std::size_t) { return everything(); }, false);
        expect("stopped transport is dry", out == input);
    }

    // History clamp: long slices at slow tempo stay within the ring.
    {
        expect("repeat slice clamp", engine.maxSliceSamples(OperationType::Repeat) * 3.0
                                         < static_cast<double>(engine.historyCapacitySamples()));
        expect("reverse slice clamp", engine.maxSliceSamples(OperationType::ReverseSlice) * 2.0 + 1.0
                                          < static_cast<double>(engine.historyCapacitySamples()));
        engine.reset();
        std::vector<float> l = sine(4096, 220.0);
        float* chans[] = {l.data()};
        VariationSettings v = everything();
        for (auto& op : v.operations)
            op.sliceBeats = 4.0f;
        for (int b = 0; b < 32; ++b)
            engine.process(chans, 1, 128, v, 1.0f, RhythmTiming{144000.0, b * 128.0 / 144000.0, true});
        bool finite = true;
        for (float s : l)
            finite = finite && std::isfinite(s);
        expect("slow tempo finite", finite);
    }

    if (failures == 0)
        std::cout << "All RhythmEngine tests passed\n";
    return failures == 0 ? 0 : 1;
}
