// C04 per-scene transition smoothing in the engine (docs/specs/W3-C04-SMOOTHING.md).
#include "core/dsp/RhythmEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace groovemorph::core;

namespace {
int failures = 0;
void expect(const std::string& name, bool ok) {
    if (!ok) { std::cerr << name << ": check failed\n"; ++failures; }
}
constexpr double pi = 3.14159265358979323846;

struct Run {
    double fs = 48000.0;
    double spb = 48000.0; // 60 BPM
    double slice = 1.0;
    int seconds = 3;
    double freq = 437.3;
    bool noise = false;
    float depth = 1.0f;
};

std::vector<float> render(const Run& r, double fadeSeconds, int block, OperationType op = OperationType::Repeat) {
    RhythmEngine engine;
    engine.prepare(r.fs, 4096, 2);
    const std::size_t total = static_cast<std::size_t>(r.fs * r.seconds);
    std::vector<float> in(total);
    std::uint32_t s = 0x9e3779b9u;
    for (std::size_t i = 0; i < total; ++i) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        in[i] = r.noise ? (static_cast<float>(s & 0xFFFF) / 32768.0f - 1.0f) * 0.3f
                        : 0.4f * static_cast<float>(std::sin(2.0 * pi * r.freq * static_cast<double>(i) / r.fs));
    }
    VariationSettings v;
    v[op] = {true, r.depth, static_cast<float>(r.slice)};
    std::vector<float> out = in;
    for (std::size_t start = 0; start < total;) {
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), total - start));
        float* ch[] = {out.data() + start, out.data() + start};
        // Both channels point at the same data; process one channel's worth by using mono.
        RhythmTiming timing;
        timing.samplesPerBeat = r.spb;
        timing.beatAtBlockStart = static_cast<double>(start) / r.spb;
        timing.isPlaying = true;
        timing.crossfadeSeconds = fadeSeconds;
        engine.process(ch, 1, n, v, 1.0f, timing);
        start += static_cast<std::size_t>(n);
    }
    return out;
}

std::uint64_t hashOf(const std::vector<float>& v) {
    std::uint64_t h = 1469598103934665603ull;
    for (float f : v) { std::uint32_t b; std::memcpy(&b, &f, 4); h ^= b; h *= 1099511628211ull; }
    return h;
}
} // namespace

int main() {
    // W3-C04S-T02: the cross-fade of a read-position jump lasts the requested number of samples.
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Run r; r.fs = fs; r.spb = fs; r.noise = true;
        const auto hard = render(r, 0.0, 512);
        for (double ms : {3.0, 25.0}) {
            const auto soft = render(r, ms * 0.001, 512);
            std::size_t first = 0;
            while (first < hard.size() && hard[first] == soft[first]) ++first;
            const auto len = static_cast<std::size_t>(std::lround(ms * 0.001 * fs));
            std::size_t diffs = 0;
            for (std::size_t i = first; i < first + len + 200 && i < hard.size(); ++i)
                if (hard[i] != soft[i]) ++diffs;
            const std::string tag = std::to_string(static_cast<int>(fs)) + " Hz " + std::to_string(static_cast<int>(ms)) + " ms";
            expect("W3-C04S-T02 a jump exists " + tag, first > 0 && first < hard.size());
            expect("W3-C04S-T02 fade length " + tag + " (" + std::to_string(diffs) + " vs " + std::to_string(len) + ")",
                   diffs + 3 >= len && diffs <= len + 3);
        }
    }

    // W3-C04S-T03: a longer fade lowers the peak step at the jump.
    {
        Run r;
        const auto step = [&](double sec) {
            const auto o = render(r, sec, 512);
            double worst = 0.0;
            for (std::size_t i = 1; i < o.size(); ++i) worst = std::max<double>(worst, std::abs(o[i] - o[i - 1]));
            return worst;
        };
        const double hard = step(0.0), mid = step(0.003), longer = step(0.05);
        const double natural = 0.4 * 2.0 * pi * r.freq / r.fs; // the input sine's own maximum step
        expect("W3-C04S-T03 a hard cut clicks (step > 5x the signal step)", hard > 5.0 * natural);
        expect("W3-C04S-T03 3 ms and 50 ms fades stay within the signal's own step", mid <= 1.02 * natural && longer <= 1.02 * natural);
        bool finite = true;
        for (float v : render(r, 0.05, 512)) finite = finite && std::isfinite(v);
        expect("W3-C04S-T03 finite", finite);
    }

    // W3-C04S-T04: block-split invariance with a 25 ms request.
    {
        Run r; r.noise = true; r.slice = 0.25; r.spb = 24000.0; r.seconds = 2;
        const auto a = hashOf(render(r, 0.025, 64)), b = hashOf(render(r, 0.025, 257)), c = hashOf(render(r, 0.025, 1024));
        expect("W3-C04S-T04 split invariant", a == b && b == c);
    }

    // W3-C04S-T05: rapid cycles with a 250 ms request stay bounded relative to the 3 ms case.
    {
        Run r; r.slice = 0.0625; r.spb = 9600.0; r.seconds = 2; r.freq = 331.0; // 1/16 beat = 600 samples = 12.5 ms per cycle
        const auto step = [&](double sec) {
            const auto o = render(r, sec, 256);
            double worst = 0.0;
            bool finite = true;
            for (std::size_t i = 1; i < o.size(); ++i) { worst = std::max<double>(worst, std::abs(o[i] - o[i - 1])); finite = finite && std::isfinite(o[i]); }
            return finite ? worst : 1.0e9;
        };
        const double base = step(0.003), long250 = step(0.25);
        expect("W3-C04S-T05 250 ms request on fast cycles stays within 1.5x of the default", long250 <= 1.5 * base + 1.0e-6);
    }

    // Default request (negative) equals an explicit 3 ms request bit for bit (also covered by EngineGoldenTests).
    {
        Run r; r.noise = true; r.seconds = 2;
        expect("W3-C04S-T01 default equals explicit 3 ms", hashOf(render(r, -1.0, 300)) == hashOf(render(r, 0.003, 300)));
        expect("W3-C04S-T01 NaN request is the default", hashOf(render(r, std::nan(""), 300)) == hashOf(render(r, -1.0, 300)));
    }

    if (failures == 0) std::cout << "TransitionSmoothingTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
