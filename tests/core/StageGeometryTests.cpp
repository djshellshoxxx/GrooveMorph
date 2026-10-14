#include "core/dsp/StageGeometry.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

using namespace groovemorph::core;
using namespace groovemorph::core::stage;

namespace {
int failures = 0;

void expect(std::string_view name, bool condition) {
    if (!condition) {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

constexpr double spb = 4000.0;
constexpr double sliceBeats = 0.25;
constexpr double len = 1000.0; // sliceBeats * spb
constexpr double t = 100000.0;
} // namespace

int main() {
    // Slot mapping and delay multiples mirror the engine's history layout.
    expect("slots", historySlot(0) == 0 && historySlot(1) == 1 && historySlot(2) == -1 && historySlot(3) == 2 && historySlot(4) == 3);
    expect("delay multiples", delayMultiple(OperationType::Repeat) == 3.0 && delayMultiple(OperationType::ReverseSlice) == 2.0
                                  && delayMultiple(OperationType::HalfTime) == 1.0 && delayMultiple(OperationType::TapeStop) == 1.0);

    // Sanitisers.
    expect("unit clamp", sanitiseUnit(2.0f, 0.5f) == 1.0f && sanitiseUnit(-1.0f, 0.5f) == 0.0f);
    expect("unit nan", sanitiseUnit(std::numeric_limits<float>::quiet_NaN(), 0.5f) == 0.5f);
    expect("slice clamp", sanitiseSliceBeats(100.0f) == 4.0 && sanitiseSliceBeats(0.0f) == 1.0 / 16.0);
    expect("slice nan", sanitiseSliceBeats(std::numeric_limits<float>::infinity()) == 0.25);

    // positionInCell: sample offset inside the cell, wrap at the end.
    expect("cell start", near(positionInCell(0.0, 0.25, spb, len), 0.0));
    expect("cell middle", near(positionInCell(0.125, 0.25, spb, len), 500.0));
    expect("cell wrap", near(positionInCell(0.25, 0.25, spb, len), 0.0));

    // Gate: open first half, closed second half.
    expect("gate open", gateOpen(0.05, sliceBeats, spb, len));
    expect("gate closed", !gateOpen(0.15, sliceBeats, spb, len));

    // Repeat: cycle of 4 slices; slice j of the cycle reads j slices back, capped at 3.
    for (int j = 0; j < 4; ++j) {
        const auto s = readPosition(OperationType::Repeat, t, (j + 0.5) * sliceBeats, sliceBeats, spb, len);
        expect("repeat lag", near(t - s.r, j * len) && s.rate == 1.0);
    }
    expect("repeat cycle restart", near(t - readPosition(OperationType::Repeat, t, 1.0, sliceBeats, spb, len).r, 0.0));

    // ReverseSlice: lag = 2p + 1, reverse rate.
    auto rev = readPosition(OperationType::ReverseSlice, t, 0.125, sliceBeats, spb, len);
    expect("reverse lag", near(t - rev.r, 2.0 * 500.0 + 1.0) && rev.rate == -1.0);
    rev = readPosition(OperationType::ReverseSlice, t, 0.0, sliceBeats, spb, len);
    expect("reverse at slice start", near(t - rev.r, 1.0));

    // HalfTime: lag = p / 2, half rate.
    auto ht = readPosition(OperationType::HalfTime, t, 0.125, sliceBeats, spb, len);
    expect("half-time", near(t - ht.r, 250.0) && ht.rate == 0.5);

    // TapeStop: lag = p^2 / (2 len); rate 1 -> 0 across the slice.
    auto ts = readPosition(OperationType::TapeStop, t, 0.125, sliceBeats, spb, len);
    expect("tape-stop mid", near(t - ts.r, 500.0 * 500.0 / 2000.0) && near(ts.rate, 0.5));
    ts = readPosition(OperationType::TapeStop, t, 0.0, sliceBeats, spb, len);
    expect("tape-stop start", near(t - ts.r, 0.0) && near(ts.rate, 1.0));

    // Gate and Count (no timing) read live.
    auto live = readPosition(OperationType::Gate, t, 0.1, sliceBeats, spb, len);
    expect("gate reads live", live.r == t && live.rate == 1.0);
    live = readPosition(OperationType::Count, t, 0.1, sliceBeats, spb, len);
    expect("count reads live", live.r == t && live.rate == 1.0);

    // Phase offset is applied by the caller: an offset of one slice shifts the cell by one slice.
    const auto shifted = readPosition(OperationType::Repeat, t, 0.25 - 0.25 + 0.3, sliceBeats, spb, len);
    expect("offset beat selects later slice", near(t - shifted.r, 1.0 * len));

    if (failures == 0)
        std::cout << "StageGeometryTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
