#include "core/dsp/LoopImport.h"

#include <cmath>
#include <iostream>
#include <string_view>

using namespace groovemorph::core;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}
bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }
} // namespace

int main() {
    // The user's case: a 4-bar loop at 140 BPM (6.857 s) with the default tempo 120 and no bar count given.
    const double s140x4 = 16.0 * 60.0 / 140.0;
    {
        const auto m = mapLoop(s140x4, 4, 120.0, 0, 0.0);
        expect("auto picks 4 bars", m.valid && near(m.bars, 4.0) && near(m.beats, 16.0));
        expect("auto reports the native tempo", near(m.nativeBpm, 140.0, 1e-6));
        expect("auto is not from bpm", !m.fromBpm);
    }
    // Same file, but the loop has 0.5 s of tail: auto is still the nearest sensible bar count, and an explicit
    // BPM keeps the tail instead of stretching it.
    {
        const double withTail = 8.0 * 60.0 / 140.0 + 0.5; // 2 bars + tail
        const auto bpm = mapLoop(withTail, 4, 120.0, 0, 140.0);
        expect("explicit bpm: fractional bars", bpm.valid && bpm.fromBpm && near(bpm.nativeBpm, 140.0) && near(bpm.beats, withTail * 140.0 / 60.0));
        expect("explicit bpm plays at original speed at 140", near(withTail / (bpm.beats * 60.0 / 140.0), 1.0));
        const auto bars = mapLoop(withTail, 4, 120.0, 2, 0.0);
        const double speedAt140 = withTail / (bars.beats * 60.0 / 140.0);
        expect("forcing 2 bars on a loop with a tail plays too fast at 140", speedAt140 > 1.1);
    }
    // Explicit bars wins over auto; bpm wins over bars.
    expect("explicit bars", near(mapLoop(4.0, 4, 120.0, 2, 0.0).beats, 8.0));
    expect("bpm beats bars", mapLoop(4.0, 4, 120.0, 2, 90.0).fromBpm);
    // Auto prefers the in-range candidate nearest the current tempo.
    {
        const auto a = mapLoop(s140x4, 4, 70.0, 0, 0.0); // 2 bars = 70 BPM is closer to 70 than 4 bars = 140
        expect("auto follows the current tempo", near(a.bars, 2.0));
    }
    // Other meters.
    expect("3/4 meter", near(mapLoop(6.0, 3, 120.0, 0, 0.0).beats, 12.0)); // 4 bars of 3 beats at 120 BPM
    // Invalid input.
    expect("zero length", !mapLoop(0.0, 4, 120.0, 0, 0.0).valid);
    expect("nan length", !mapLoop(std::nan(""), 4, 120.0, 0, 0.0).valid);
    expect("bad meter", !mapLoop(4.0, 0, 120.0, 0, 0.0).valid);
    expect("nan tempo falls back to 120", mapLoop(4.0, 4, std::nan(""), 0, 0.0).valid);

    if (failures == 0) std::cout << "LoopImportTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
