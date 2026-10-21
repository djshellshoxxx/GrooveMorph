// N01: FilterSweep and BitCrush (docs/specs/N01-EFFECTS-SWEEP-CRUSH.md), acceptance tests N01-T01..T08 and T12.
#include "core/dsp/RhythmEngine.h"
#include "core/dsp/SweepCrush.h"
#include "core/trace/TraceRing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace groovemorph::core;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}
void expectNear(std::string_view name, double actual, double expected, double tolerance) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        std::cerr << name << ": got " << actual << ", expected " << expected << " +/- " << tolerance << "\n";
        ++failures;
    }
}

constexpr double pi = 3.14159265358979323846;

std::vector<float> noise(std::size_t n, float amplitude, std::uint32_t seed = 0x2468ACEu) {
    std::vector<float> v(n);
    std::uint32_t s = seed;
    for (auto& x : v) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        x = (static_cast<float>(s & 0xFFFF) / 32768.0f - 1.0f) * amplitude;
    }
    return v;
}

VariationSettings sceneWith(OperationType op, float depth, float slice, bool enabled = true) {
    VariationSettings v;
    v[op] = {enabled, depth, slice};
    return v;
}

struct Config {
    double sampleRate = 48000.0;
    double spb = 24000.0; // samples per beat
    int channels = 1;
};

// Runs the engine over `input` (mono, copied to every channel) in blocks of `block`; returns channel 0.
std::vector<float> run(const Config& cfg, const VariationSettings& scene, const std::vector<float>& input, int block,
                       float intensity = 1.0f, bool playing = true, RhythmEngine* external = nullptr, double startSample = 0.0) {
    RhythmEngine local;
    RhythmEngine& engine = external != nullptr ? *external : local;
    if (external == nullptr)
        engine.prepare(cfg.sampleRate, 4096, cfg.channels);
    std::vector<std::vector<float>> data(static_cast<std::size_t>(cfg.channels), input);
    for (std::size_t start = 0; start < input.size();) {
        const std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(block), input.size() - start);
        std::vector<float*> ch;
        for (auto& d : data) ch.push_back(d.data() + start);
        engine.process(ch.data(), cfg.channels, static_cast<int>(len), scene, intensity,
                       RhythmTiming{cfg.spb, (startSample + static_cast<double>(start)) / cfg.spb, playing});
        start += len;
    }
    return data[0];
}

bool identical(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

double rms(const std::vector<float>& v, std::size_t from, std::size_t count) {
    double sum = 0.0;
    for (std::size_t i = from; i < from + count; ++i) sum += static_cast<double>(v[i]) * v[i];
    return std::sqrt(sum / static_cast<double>(count));
}

// |H| of the TPT low-pass at frequency f for cutoff fc (analog prototype with bilinear warping).
double lowPassGain(double f, double fc, double sampleRate) {
    const double g = std::tan(pi * fc / sampleRate);
    const double w = std::tan(pi * f / sampleRate) / g;
    const double a = 1.0 - w * w, b = w / stage::sweepQ;
    return 1.0 / std::sqrt(a * a + b * b);
}

double db(double x) { return 20.0 * std::log10(std::max(x, 1.0e-12)); }

// N01-T01 / T02: frequency response follows the cutoff trajectory; stable at every rate.
void sweepFrequencyResponse() {
    for (const double sr : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) {
        Config cfg;
        cfg.sampleRate = sr;
        cfg.spb = 0.5 * sr; // 4-beat slice = 2 s per sweep: slow enough to read a steady response
        const double sliceSamples = 4.0 * cfg.spb;
        const auto scene = sceneWith(OperationType::FilterSweep, 1.0f, 4.0f);
        const std::size_t total = static_cast<std::size_t>(2.0 * sliceSamples); // slice 0 closes, slice 1 opens
        for (const double f0 : {200.0, 1000.0, 6000.0}) {
            const double f = std::min(f0, 0.3 * sr);
            std::vector<float> input(total);
            for (std::size_t i = 0; i < total; ++i)
                input[i] = static_cast<float>(0.5 * std::sin(2.0 * pi * f * static_cast<double>(i) / sr));
            const auto out = run(cfg, scene, input, 480);
            const std::size_t win = static_cast<std::size_t>(0.02 * sr);
            for (const double slicePos : {0.1, 0.4, 0.7, 1.25, 1.6}) { // slice index = floor, odd slices open
                const double p = slicePos - std::floor(slicePos);
                const bool opening = std::floor(slicePos) > 0.5;
                const std::size_t centre = static_cast<std::size_t>(slicePos * sliceSamples);
                const std::size_t from = centre - win / 2;
                const double measured = db(rms(out, from, win) / rms(input, from, win));
                const double fc = stage::sweepCutoffHz(p, opening, sr);
                const double expected = db(lowPassGain(f, fc, sr));
                const std::string name = "sweep response sr=" + std::to_string(static_cast<int>(sr)) + " f="
                                         + std::to_string(static_cast<int>(f)) + " pos=" + std::to_string(slicePos);
                expectNear(name, measured, expected, 1.2);
            }
        }
    }
    // Direction and range of the trajectory.
    expectNear("closing sweep starts at fHigh", stage::sweepCutoffHz(0.0, false, 48000.0), 20000.0, 1e-9);
    expectNear("closing sweep ends near 200 Hz", stage::sweepCutoffHz(1.0, false, 48000.0), 200.0, 1e-9);
    expectNear("opening sweep starts at 200 Hz", stage::sweepCutoffHz(0.0, true, 48000.0), 200.0, 1e-9);
    expectNear("sweep is exponential: midpoint is the geometric mean", stage::sweepCutoffHz(0.5, false, 48000.0), std::sqrt(20000.0 * 200.0), 1e-6);
    expect("8 kHz cutoff stays below Nyquist", stage::sweepCutoffHz(0.0, false, 8000.0) < 4000.0);
}

void sweepStability() {
    for (const double sr : {8000.0, 22050.0, 48000.0, 96000.0, 384000.0}) {
        for (const float slice : {1.0f / 16.0f, 0.25f, 4.0f}) {
            Config cfg;
            cfg.sampleRate = sr;
            cfg.spb = sr / 4.0;
            const std::size_t total = static_cast<std::size_t>(std::min(sr * 1.5, 200000.0));
            const auto input = noise(total, 1.0f);
            const auto out = run(cfg, sceneWith(OperationType::FilterSweep, 1.0f, slice), input, 333);
            bool finite = true;
            float peak = 0.0f;
            for (float y : out) { finite = finite && std::isfinite(y); peak = std::max(peak, std::abs(y)); }
            expect("sweep output finite at rate " + std::to_string(static_cast<int>(sr)), finite);
            expect("sweep bounded (peak < 8) at rate " + std::to_string(static_cast<int>(sr)), peak < 8.0f);
        }
    }
    // Coefficients are finite and positive at the extremes.
    for (const double sr : {8000.0, 384000.0})
        for (const double fc : {stage::sweepCutoffHz(0.0, false, sr), stage::sweepCutoffHz(1.0, false, sr)}) {
            const auto c = stage::svfCoeffs(fc, sr);
            expect("coefficients finite and positive", std::isfinite(c.a1) && std::isfinite(c.a2) && std::isfinite(c.a3) && c.a1 > 0.0 && c.a2 > 0.0 && c.a3 > 0.0);
        }
}

// N01-T03: quantisation levels and hold length follow the envelope.
void crushLevels() {
    expectNear("bits at envelope 0", stage::crushBits(0.0), 16.0, 1e-12);
    expectNear("bits at envelope 1", stage::crushBits(1.0), 4.0, 1e-12);
    expectNear("rate at envelope 0", stage::crushRateHz(0.0), 12000.0, 1e-9);
    expectNear("rate at envelope 1", stage::crushRateHz(1.0), 750.0, 1e-9);
    expectNear("hold is 1 when the target rate is above the host rate", stage::crushHold(0.0, 8000.0), 1.0, 1e-12);
    expectNear("hold at 48 kHz / 12 kHz", stage::crushHold(0.0, 48000.0), 4.0, 1e-12);

    // 4-bit quantiser: 17 distinct levels over [-1, 1], all multiples of 1/8.
    std::vector<float> levels;
    for (int i = -4000; i <= 4000; ++i) {
        const float q = stage::crushQuantise(static_cast<float>(i) / 4000.0f, 4.0);
        expectNear("4-bit output is a multiple of 1/8", std::abs(q * 8.0 - std::nearbyint(q * 8.0)), 0.0, 1e-6);
        if (levels.empty() || std::find(levels.begin(), levels.end(), q) == levels.end()) levels.push_back(q);
    }
    expect("4-bit quantiser has 17 levels over [-1, 1]", levels.size() == 17);

    // Engine: every output sample is a multiple of the quantiser step at its own envelope position.
    Config cfg;
    cfg.spb = 4800.0;
    const double sliceSamples = cfg.spb; // 1 beat
    const std::size_t total = static_cast<std::size_t>(4.0 * sliceSamples);
    std::vector<float> ramp(total);
    for (std::size_t i = 0; i < total; ++i)
        ramp[i] = static_cast<float>(std::fmod(static_cast<double>(i) * 0.000713, 1.8) - 0.9);
    const auto out = run(cfg, sceneWith(OperationType::BitCrush, 1.0f, 1.0f), ramp, 480);
    int checked = 0;
    for (std::size_t i = 1; i < total; ++i) {
        if (std::abs(out[i] - out[i - 1]) <= 1.0e-5f) continue; // not a capture instant
        const double e = std::fmod(static_cast<double>(i), sliceSamples) / sliceSamples;
        const double step = 1.0 / std::exp2(stage::crushBits(e) - 1.0);
        const double r = std::abs(out[i] / step - std::nearbyint(out[i] / step));
        if (r > 2e-3) { expectNear("crush capture on a level at sample " + std::to_string(i), r, 0.0, 2e-3); break; }
        ++checked;
    }
    expect("many captures checked", checked > 1000);
    // Late in the slice the output has only ~2^bits levels.
    {
        std::vector<float> distinct;
        const std::size_t from = static_cast<std::size_t>(0.95 * sliceSamples), to = static_cast<std::size_t>(sliceSamples);
        for (std::size_t i = from; i < to; ++i)
        {
            const float key = std::nearbyint(out[i] * 1.0e4f) / 1.0e4f;
            if (std::find(distinct.begin(), distinct.end(), key) == distinct.end()) distinct.push_back(key);
        }
        expect("few levels late in the slice", distinct.size() <= 20);
    }
}

int countChanges(const std::vector<float>& v, std::size_t from, std::size_t to) {
    int n = 0;
    for (std::size_t i = from + 1; i < to; ++i) n += (std::abs(v[i] - v[i - 1]) > 1.0e-5f) ? 1 : 0; // ignore 1-ulp rounding of x + a * (wet - x)
    return n;
}

void crushHoldLength() {
    // Captures per second at envelope ~0.5 equal the target rate (3 kHz) at any host rate.
    for (const double sr : {8000.0, 48000.0, 192000.0}) {
        Config cfg;
        cfg.sampleRate = sr;
        cfg.spb = sr; // 1 beat = 1 s
        const auto input = noise(static_cast<std::size_t>(sr), 0.9f);
        const auto out = run(cfg, sceneWith(OperationType::BitCrush, 1.0f, 1.0f), input, 256);
        const std::size_t from = static_cast<std::size_t>(0.49 * sr), to = static_cast<std::size_t>(0.51 * sr);
        const double e = 0.5;
        const double expected = stage::crushRateHz(e) * 0.02; // captures in 20 ms
        const int changes = countChanges(out, from, to);
        expectNear("hold captures per 20 ms at sr " + std::to_string(static_cast<int>(sr)), changes, expected, 0.1 * expected + 2.0);
    }
}

// N01-T04: bit-exact bypass.
void bypass() {
    Config cfg;
    const auto input = noise(20000, 0.8f);
    for (const auto op : {OperationType::FilterSweep, OperationType::BitCrush}) {
        const std::string n = op == OperationType::FilterSweep ? "sweep" : "crush";
        expect(n + ": intensity 0 is bit-exact", identical(run(cfg, sceneWith(op, 1.0f, 0.25f), input, 256, 0.0f), input));
        expect(n + ": depth 0 is bit-exact", identical(run(cfg, sceneWith(op, 0.0f, 0.25f), input, 256), input));
        expect(n + ": disabled is bit-exact", identical(run(cfg, sceneWith(op, 1.0f, 0.25f, false), input, 256), input));
        expect(n + ": stopped transport is bit-exact", identical(run(cfg, sceneWith(op, 1.0f, 0.25f), input, 256, 1.0f, false), input));
        expect(n + ": active effect changes the signal", !identical(run(cfg, sceneWith(op, 1.0f, 0.25f), input, 256), input));
    }
}

// N01-T05: output does not depend on how the host splits the stream.
void blockSplit() {
    for (const double sr : {44100.0, 96000.0}) {
        Config cfg;
        cfg.sampleRate = sr;
        cfg.spb = sr * 0.3;
        cfg.channels = 2;
        VariationSettings scene;
        scene[OperationType::FilterSweep] = {true, 0.8f, 0.5f};
        scene[OperationType::BitCrush] = {true, 0.9f, 0.25f};
        scene[OperationType::Gate] = {true, 0.5f, 0.5f};
        const auto input = noise(static_cast<std::size_t>(sr * 0.6), 0.7f);
        const auto reference = run(cfg, scene, input, 4096);
        for (const int block : {1, 7, 64, 480, 1000}) {
            if (block == 1 && sr > 50000.0) continue; // keep the run short
            expect("block split " + std::to_string(block) + " at " + std::to_string(static_cast<int>(sr)) + " Hz",
                   identical(run(cfg, scene, input, block), reference));
        }
    }
}

// N01-T06: NaN, Inf and denormal inputs.
void hostileInput() {
    Config cfg;
    auto dirty = noise(30000, 0.6f);
    auto clean = dirty;
    for (std::size_t i : {100u, 101u, 5000u, 12345u, 12346u, 12347u}) {
        dirty[i] = (i % 2) ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
        clean[i] = 0.0f;
    }
    dirty[20000] = -std::numeric_limits<float>::infinity();
    clean[20000] = 0.0f;
    VariationSettings scene;
    scene[OperationType::FilterSweep] = {true, 1.0f, 0.25f};
    scene[OperationType::BitCrush] = {true, 1.0f, 0.5f};
    const auto a = run(cfg, scene, dirty, 512);
    const auto b = run(cfg, scene, clean, 512);
    bool finite = true;
    for (float y : a) finite = finite && std::isfinite(y);
    expect("non-finite input never reaches the output", finite);
    expect("non-finite input behaves like silence for that sample (no state poisoning)", identical(a, b));

    // Huge finite input stays finite.
    std::vector<float> huge(5000, 3.0e38f);
    const auto h = run(cfg, sceneWith(OperationType::FilterSweep, 1.0f, 0.25f), huge, 256);
    bool hugeFinite = true;
    for (float y : h) hugeFinite = hugeFinite && std::isfinite(y);
    expect("near-overflow input stays finite", hugeFinite);

    // Denormal burst then silence: no subnormal output, and the tail settles to exact zero.
    std::vector<float> burst(480000, 0.0f);
    for (std::size_t i = 0; i < 64; ++i) burst[i] = 1.0e-38f * (i % 2 ? 1.0f : -1.0f);
    burst[100] = 1.0f;
    for (const auto op : {OperationType::FilterSweep, OperationType::BitCrush}) {
        const auto out = run(cfg, sceneWith(op, 1.0f, 0.25f), burst, 512);
        bool subnormal = false;
        for (float y : out) subnormal = subnormal || std::fpclassify(y) == FP_SUBNORMAL;
        expect("no subnormal output", !subnormal);
        bool settled = true;
        for (std::size_t i = out.size() - 200; i < out.size(); ++i) settled = settled && out[i] == 0.0f;
        expect("tail settles to exactly zero", settled);
    }
}

// N01-T07: a transport jump (resetHistory) clears the effect state.
void resetOnSeek() {
    Config cfg;
    cfg.channels = 2;
    VariationSettings scene;
    scene[OperationType::FilterSweep] = {true, 1.0f, 0.5f};
    scene[OperationType::BitCrush] = {true, 1.0f, 0.5f};
    const auto garbage = noise(30000, 0.9f, 1234u);
    const auto probe = noise(9000, 0.7f, 99u);

    RhythmEngine used;
    used.prepare(cfg.sampleRate, 4096, cfg.channels);
    (void)run(cfg, scene, garbage, 512, 1.0f, true, &used);
    used.resetHistory();
    const auto after = run(cfg, scene, probe, 512, 1.0f, true, &used, 0.0);

    RhythmEngine fresh;
    fresh.prepare(cfg.sampleRate, 4096, cfg.channels);
    (void)run(cfg, scene, std::vector<float>(10, 0.0f), 10, 1.0f, true, &fresh); // initialise amounts like the used engine
    fresh.resetHistory();
    const auto expected = run(cfg, scene, probe, 512, 1.0f, true, &fresh, 0.0);
    expect("output after a seek equals a fresh engine's", identical(after, expected));
}

// N01-T08: no history ring and no memory growth.
void memory() {
    RhythmEngine a, b;
    a.prepare(48000.0, 512, 2);
    b.prepare(48000.0, 512, 2);
    Config cfg;
    cfg.channels = 2;
    (void)run(cfg, sceneWith(OperationType::FilterSweep, 1.0f, 1.0f), noise(5000, 0.5f), 256, 1.0f, true, &a);
    expect("history capacity does not depend on the new stages", a.historyCapacitySamples() == b.historyCapacitySamples());
    expect("new stages read no history slot", stage::historySlot(static_cast<std::size_t>(OperationType::FilterSweep)) < 0
                                               && stage::historySlot(static_cast<std::size_t>(OperationType::BitCrush)) < 0);
    expect("chain order follows the enum", static_cast<std::size_t>(OperationType::FilterSweep) == 5
                                           && static_cast<std::size_t>(OperationType::BitCrush) == 6 && operationCount == 7);
}

// Dry/wet: a half-depth stage sits halfway between the dry and the fully effected signal.
void depthIsAmount() {
    Config cfg;
    const auto input = noise(20000, 0.8f);
    for (const auto op : {OperationType::FilterSweep, OperationType::BitCrush}) {
        const auto full = run(cfg, sceneWith(op, 1.0f, 0.25f), input, 256);
        const auto half = run(cfg, sceneWith(op, 0.5f, 0.25f), input, 256);
        double worst = 0.0;
        for (std::size_t i = 0; i < input.size(); ++i)
            worst = std::max(worst, static_cast<double>(std::abs(half[i] - (input[i] + 0.5f * (full[i] - input[i])))));
        expectNear("depth is the stage amount", worst, 0.0, 1e-5);
    }
}

// N01-T12: trace frames carry all seven amounts.
void traceAmounts() {
    RhythmEngine engine;
    engine.prepare(48000.0, 1024, 1);
    trace::TraceRing ring;
    ring.setActive(true);
    engine.setTraceSink(&ring);
    VariationSettings scene;
    scene[OperationType::FilterSweep] = {true, 0.5f, 0.25f};
    scene[OperationType::BitCrush] = {true, 0.25f, 0.25f};
    Config cfg;
    (void)run(cfg, scene, noise(20000, 0.5f), 512, 1.0f, true, &engine);
    std::vector<trace::TraceFrame> frames(trace::ringCapacity);
    frames.resize(ring.copyLatest(frames.data(), frames.size()));
    expect("frames recorded", !frames.empty());
    const auto& last = frames.back();
    expectNear("sweep amount in trace", last.amount[static_cast<std::size_t>(OperationType::FilterSweep)], 0.5, 1e-6);
    expectNear("crush amount in trace", last.amount[static_cast<std::size_t>(OperationType::BitCrush)], 0.25, 1e-6);
    expectNear("unused amount is zero", last.amount[0], 0.0, 1e-9);
    expect("trace stage count equals operation count", trace::stageCount == operationCount);
}

// Click-freedom: enabling the stage mid-stream never produces a step larger than the input's own movement.
void enableIsSmooth() {
    Config cfg;
    cfg.spb = 24000.0;
    std::vector<float> input(30000);
    for (std::size_t i = 0; i < input.size(); ++i) input[i] = static_cast<float>(0.5 * std::sin(2.0 * pi * 220.0 * static_cast<double>(i) / 48000.0));
    RhythmEngine engine;
    engine.prepare(48000.0, 4096, 1);
    auto first = run(cfg, VariationSettings{}, std::vector<float>(input.begin(), input.begin() + 10000), 500, 1.0f, true, &engine);
    std::vector<float> tail(input.begin() + 10000, input.end());
    auto second = run(cfg, sceneWith(OperationType::FilterSweep, 1.0f, 4.0f), tail, 500, 1.0f, true, &engine, 10000.0);
    float maxStep = 0.0f;
    for (std::size_t i = 1; i < second.size(); ++i) maxStep = std::max(maxStep, std::abs(second[i] - second[i - 1]));
    expect("no click when the sweep is enabled (max sample step stays at the 220 Hz sine's own)", maxStep < 0.05f);
}
} // namespace

int main() {
    sweepFrequencyResponse();
    sweepStability();
    crushLevels();
    crushHoldLength();
    bypass();
    blockSplit();
    hostileInput();
    resetOnSeek();
    memory();
    depthIsAmount();
    traceAmounts();
    enableIsSmooth();
    if (failures == 0) std::cout << "EffectsSweepCrushTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
