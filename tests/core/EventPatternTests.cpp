#include "core/control/EventPattern.h"

#include <iostream>
#include <limits>
#include <string_view>

using namespace groovemorph::core;
using namespace groovemorph::core::control;

namespace {
int failures = 0;

void expect(std::string_view name, bool condition) {
    if (!condition) {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

PhraseEvent ev(std::uint32_t id, OperationType op, std::int64_t onset, std::int64_t dur) {
    PhraseEvent e;
    e.id = id;
    e.operation = op;
    e.onsetTicks = onset;
    e.durationTicks = dur;
    return e;
}

EventPattern pattern(std::initializer_list<PhraseEvent> list) {
    EventPattern p;
    p.kind = PatternKind::ExplicitEvents;
    for (const auto& e : list)
        p.events[p.count++] = e;
    return p;
}
} // namespace

int main() {
    // Tick arithmetic.
    expect("4/4 beat", meterBeatTicks(4) == 960);
    expect("6/8 beat", meterBeatTicks(8) == 480);
    expect("bad denominator", !meterBeatTicks(3) && !meterBeatTicks(0) && !meterBeatTicks(64));
    expect("4 bars of 4/4", phraseTicks(4, 4, 4) == 4 * 4 * 960);
    expect("max phrase", phraseTicks(32, 32, 1) == 32 * 32 * 3840);
    expect("phrase rejects zero/over", !phraseTicks(0, 4, 4) && !phraseTicks(33, 4, 4) && !phraseTicks(1, 0, 4));
    for (int s : {1, 2, 3, 4, 6, 8, 12, 16})
        expect("grid divides quarter beat", gridTicks(960, s) == 960 / s);
    expect("grid rejects unsupported", !gridTicks(960, 5) && !gridTicks(960, 0));
    expect("grid rejects inexact", !gridTicks(120, 16));
    expect("floor modulo", positiveModulo(-1, 10) == 9 && positiveModulo(-20, 10) == 0 && positiveModulo(23, 10) == 3);

    const std::int64_t P = 3840 * 4;
    expect("legacy empty valid", validatePattern(EventPattern{}, P) == PatternStatus::Valid);
    EventPattern legacyWithEvent;
    legacyWithEvent.count = 1;
    expect("legacy with events invalid", validatePattern(legacyWithEvent, P) == PatternStatus::Invalid);
    expect("bad phrase length", validatePattern(EventPattern{}, 0) == PatternStatus::Invalid);

    // C01-T05: 256 events succeed, 257 rejected.
    {
        EventPattern full;
        full.kind = PatternKind::ExplicitEvents;
        for (std::uint32_t i = 0; i < maxEvents; ++i)
            full.events[full.count++] = ev(i + 1, OperationType::Gate, static_cast<std::int64_t>(i) * 60, 60);
        expect("256 events valid", validatePattern(full, 256 * 60) == PatternStatus::Valid);
        full.count = static_cast<std::uint16_t>(maxEvents + 1);
        expect("257 events capacity", validatePattern(full, 256 * 60) == PatternStatus::Capacity);
    }

    // Overlap rules.
    expect("touching allowed", validatePattern(pattern({ev(1, OperationType::Repeat, 0, 960), ev(2, OperationType::Repeat, 960, 960)}), P) == PatternStatus::Valid);
    expect("overlap rejected", validatePattern(pattern({ev(1, OperationType::Repeat, 0, 961), ev(2, OperationType::Repeat, 960, 960)}), P) == PatternStatus::Overlap);
    expect("same onset overlap", validatePattern(pattern({ev(1, OperationType::Gate, 100, 10), ev(2, OperationType::Gate, 100, 10)}), P) == PatternStatus::Overlap);
    expect("wraparound overlap", validatePattern(pattern({ev(1, OperationType::Gate, P - 100, 200), ev(2, OperationType::Gate, 50, 10)}), P) == PatternStatus::Overlap);
    expect("wraparound touching", validatePattern(pattern({ev(1, OperationType::Gate, P - 100, 100), ev(2, OperationType::Gate, 0, 10)}), P) == PatternStatus::Valid);
    expect("full phrase blocks same op", validatePattern(pattern({ev(1, OperationType::Gate, 0, P), ev(2, OperationType::Gate, 500, 10)}), P) == PatternStatus::Overlap);
    expect("different ops may overlap", validatePattern(pattern({ev(1, OperationType::Gate, 0, P), ev(2, OperationType::Repeat, 0, P)}), P) == PatternStatus::Valid);
    {
        auto p = pattern({ev(1, OperationType::Gate, 0, 100), ev(2, OperationType::Gate, 50, 100)});
        p.events[1].enabled = false;
        expect("disabled events still conflict", validatePattern(p, P) == PatternStatus::Overlap);
    }

    // Invalid fields.
    expect("zero id", validatePattern(pattern({ev(0, OperationType::Gate, 0, 10)}), P) == PatternStatus::Invalid);
    expect("duplicate id", validatePattern(pattern({ev(1, OperationType::Gate, 0, 10), ev(1, OperationType::Repeat, 0, 10)}), P) == PatternStatus::Invalid);
    expect("zero duration", validatePattern(pattern({ev(1, OperationType::Gate, 0, 0)}), P) == PatternStatus::Invalid);
    expect("duration over phrase", validatePattern(pattern({ev(1, OperationType::Gate, 0, P + 1)}), P) == PatternStatus::Invalid);
    expect("negative onset", validatePattern(pattern({ev(1, OperationType::Gate, -1, 10)}), P) == PatternStatus::Invalid);
    expect("onset at phrase end", validatePattern(pattern({ev(1, OperationType::Gate, P, 10)}), P) == PatternStatus::Invalid);
    expect("operation Count", validatePattern(pattern({ev(1, OperationType::Count, 0, 10)}), P) == PatternStatus::Invalid);
    {
        auto p = pattern({ev(1, OperationType::Gate, 0, 10)});
        p.events[0].depth = std::numeric_limits<float>::quiet_NaN();
        expect("NaN depth", validatePattern(p, P) == PatternStatus::Invalid);
        p.events[0].depth = 1.0f;
        p.events[0].sliceBeats = 0.01f;
        expect("slice too small", validatePattern(p, P) == PatternStatus::Invalid);
        p.events[0].sliceBeats = 4.0f;
        p.events[0].priority = 101;
        expect("priority over 100", validatePattern(p, P) == PatternStatus::Invalid);
        p.events[0].priority = 100;
        p.events[0].preserveCycleAnchor = true;
        p.events[0].cycleAnchorTicks = P;
        expect("anchor at phrase end", validatePattern(p, P) == PatternStatus::Invalid);
        p.events[0].cycleAnchorTicks = P - 1;
        expect("anchor in range", validatePattern(p, P) == PatternStatus::Valid);
    }

    if (failures == 0)
        std::cout << "EventPatternTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
