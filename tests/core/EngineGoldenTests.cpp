// Golden output hashes of RhythmEngine, captured BEFORE the visual-feedback changes (S2/S3).
// Any change to engine arithmetic or read positions shows up here as a hash mismatch.
// Run with --print to regenerate (only when an audible change is intended and documented).
#include "core/dsp/RhythmEngine.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace groovemorph::core;

namespace {
constexpr double sampleRate = 48000.0;
constexpr double samplesPerBeat = 4800.0; // 600 BPM keeps fixtures short
constexpr std::size_t totalSamples = 96000;

std::vector<float> makeInput() {
    std::vector<float> v(totalSamples);
    std::uint32_t s = 0x1234567u;
    for (std::size_t i = 0; i < v.size(); ++i) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5; // xorshift32
        const float noise = (static_cast<float>(s & 0xFFFF) / 32768.0f - 1.0f) * 0.15f;
        // Triangle wave from integer arithmetic: no libm, so the fixture is identical on every platform.
        const std::uint32_t ph = static_cast<std::uint32_t>((i * 220u * 65536u / 48000u) & 0xFFFFu);
        const float tri = (ph < 32768u ? static_cast<float>(ph) : static_cast<float>(65536u - ph)) / 16384.0f - 1.0f;
        v[i] = noise + 0.4f * tri;
    }
    return v;
}

std::uint64_t fnv(const std::vector<float>& v) {
    std::uint64_t h = 1469598103934665603ull;
    for (float f : v) {
        std::uint32_t bits;
        std::memcpy(&bits, &f, sizeof bits);
        for (int k = 0; k < 4; ++k) { h ^= (bits >> (8 * k)) & 0xFF; h *= 1099511628211ull; }
    }
    return h;
}

VariationSettings scene(int which, bool alt) {
    VariationSettings v;
    auto set = [&](OperationType t, float slice, float depth) { v[t] = {true, depth, slice}; };
    switch (which) {
        case 0: set(OperationType::Repeat, alt ? 0.5f : 0.25f, 1.0f); break;
        case 1: set(OperationType::ReverseSlice, alt ? 1.0f : 0.5f, 1.0f); break;
        case 2: set(OperationType::Gate, alt ? 0.25f : 0.5f, 1.0f); break;
        case 3: set(OperationType::HalfTime, alt ? 1.0f : 0.5f, 1.0f); break;
        case 4: set(OperationType::TapeStop, alt ? 1.0f : 0.5f, 1.0f); break;
        default:
            set(OperationType::Repeat, 0.25f, 0.8f); set(OperationType::ReverseSlice, 0.5f, 0.6f);
            set(OperationType::Gate, 0.5f, 0.5f); set(OperationType::HalfTime, 1.0f, 0.7f);
            set(OperationType::TapeStop, 1.0f, alt ? 0.9f : 0.4f);
            break;
    }
    return v;
}

// Fixture: scene `which`, switching to the alternate slice settings halfway (exercises crossfade taps).
std::uint64_t run(int which, int block, float intensity, double fadeSeconds = -1.0) {
    RhythmEngine engine;
    engine.prepare(sampleRate, 1024, 2);
    std::vector<float> left = makeInput(), right = left;
    for (std::size_t start = 0; start < totalSamples;) {
        std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(block), totalSamples - start);
        const std::size_t sw = totalSamples / 2; // the scene switch lands on an exact sample for every block size
        if (start < sw && start + len > sw) len = sw - start;
        const int n = static_cast<int>(len);
        float* chans[] = {left.data() + start, right.data() + start};
        RhythmTiming timing{samplesPerBeat, static_cast<double>(start) / samplesPerBeat, true};
        timing.crossfadeSeconds = fadeSeconds;
        engine.process(chans, 2, n, scene(which, start >= totalSamples / 2), intensity, timing);
        start += len;
    }
    return fnv(left) ^ (left == right ? 0 : 0xDEADBEEFull);
}

struct Case { int which; float intensity; std::uint64_t expected; };
} // namespace

int main(int argc, char** argv) {
    const bool print = argc > 1 && std::string(argv[1]) == "--print";
    // Captured with --print from the engine before the visual-feedback changes (commit 3ace82d lineage).
#if defined(__arm64__) || defined(__aarch64__)
    // ARM64 uses different floating-point code generation than the x86 fixtures below.
    // Captured from the universal macOS ARM64 slice; split-invariance is still checked separately.
    static const Case cases[] = {
        {0, 1.0f, 0x9399db518d1e5d35ull},
        {1, 1.0f, 0x649bdc0b714a1d95ull},
        {2, 1.0f, 0x3ea34ee76438c39bull},
        {3, 1.0f, 0x35e15e1bdfa6ea77ull},
        {4, 1.0f, 0x84a7027b6f4d9a26ull},
        {5, 1.0f, 0x0ac8402b08a19726ull},
        {5, 0.8f, 0x938c81491c7ebde6ull},
    };
#else
    static const Case cases[] = {
        {0, 1.0f, 0x3b84ca641ed65247ull},
        {1, 1.0f, 0x6942f7a964b3d1f6ull},
        {2, 1.0f, 0xc053d3c825e83b6ull},
        {3, 1.0f, 0xbebe60789a859721ull},
        {4, 1.0f, 0x91253ee10bc71d1bull},
        {5, 1.0f, 0x6b28fb68ddbe3bf0ull},
        {5, 0.8f, 0xd39ae5188a08655cull},
    };
#endif
    int failures = 0;
    for (const auto& c : cases) {
        const auto h128 = run(c.which, 128, c.intensity);
        const auto h513 = run(c.which, 513, c.intensity);
        if (print) { std::cout << "{" << c.which << ", " << c.intensity << "f, 0x" << std::hex << h128 << std::dec << "ull}, // split-invariant=" << (h128 == h513) << "\n"; continue; }
        if (h128 != c.expected) { std::cerr << "golden mismatch case " << c.which << " intensity " << c.intensity << ": got 0x" << std::hex << h128 << "\n"; ++failures; }
        if (h128 != h513) { std::cerr << "block-split dependence in case " << c.which << "\n"; ++failures; }
        // W3 C04 smoothing: an explicit request equal to the engine default (3 ms) is bit-identical to the legacy path.
        if (run(c.which, 128, c.intensity, 0.003) != c.expected) { std::cerr << "explicit 3 ms differs from default in case " << c.which << "\n"; ++failures; }
    }
    if (!print && failures == 0) std::cout << "EngineGoldenTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
