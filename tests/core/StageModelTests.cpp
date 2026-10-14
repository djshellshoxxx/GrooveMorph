#include "core/viz/StageModel.h"

#include <iostream>
#include <limits>
#include <string>
#include <string_view>

using namespace groovemorph::core;
using namespace groovemorph::core::viz;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}
template <typename A, typename B>
void expectEq(std::string_view name, const A& a, const B& b) {
    if (!(a == b)) { std::cerr << name << ": got \"" << a << "\" expected \"" << b << "\"\n"; ++failures; }
}
} // namespace

int main() {
    expectEq("1/4", formatSlice(0.25f), std::string("1/4 beat"));
    expectEq("1/16", formatSlice(0.0625f), std::string("1/16 beat"));
    expectEq("1/2", formatSlice(0.5f), std::string("1/2 beat"));
    expectEq("1", formatSlice(1.0f), std::string("1 beat"));
    expectEq("2", formatSlice(2.0f), std::string("2 beats"));
    expectEq("odd", formatSlice(0.3f), std::string("0.30 beats"));
    expectEq("zero", formatSlice(0.0f), std::string("-"));
    expectEq("nan", formatSlice(std::numeric_limits<float>::quiet_NaN()), std::string("-"));

    VariationSettings scene;
    scene[OperationType::Repeat] = {true, 1.0f, 0.25f};
    scene[OperationType::Gate] = {true, 0.8f, 0.5f};
    scene[OperationType::HalfTime] = {true, 1.0f, 1.0f};
    VariationSettings now = scene;
    now[OperationType::HalfTime].enabled = false; // gated off by placement / limit right now
    std::array<std::uint8_t, operationCount> locks{};
    locks[static_cast<std::size_t>(OperationType::ReverseSlice)] = 2;

    trace::TraceFrame f;
    f.flags = trace::TimingOk;
    f.amount[0] = 1.0f;
    f.amount[2] = 0.8f;
    f.gateGain = 0.0f;
    {
        const auto s = buildStages(scene, now, locks, &f);
        expect("repeat active", s[0].state == StageState::Active && s[0].amount == 1.0f && s[0].text == "1/4 beat");
        expect("reverse bypassed but locked", s[1].state == StageState::Bypassed && s[1].locked);
        expect("gate active with level", s[2].state == StageState::Active && s[2].text == "1/2 beat, -14.0 dB");
        expect("half-time held off", s[3].state == StageState::HeldOff && s[3].text == "1 beat");
        expect("tape-stop bypassed", s[4].state == StageState::Bypassed && s[4].text.empty());
        expect("names and effects", s[0].name == "Repeat" && s[4].effect == Effect::TapeStop && s[0].timed);
    }
    {
        // Transport stopped: enabled effects are bypassed, not "active".
        trace::TraceFrame stopped;
        const auto s = buildStages(scene, now, locks, &stopped);
        expect("stopped: nothing active", s[0].state == StageState::Bypassed && !s[0].timed);
        const auto none = buildStages(scene, now, locks, nullptr);
        expect("no frames: nothing active", none[0].state == StageState::Bypassed && none[0].amount == 0.0f);
    }

    {
        const auto o = buildOrbit(0, 2, 31200, 24000.0, 1, 0.35f, 0, false);
        expect("sounding and queued", o.nodes[0].sounding && o.nodes[2].queued && !o.nodes[1].sounding);
        expect("countdown beats", std::abs(o.countdownBeats - 1.3) < 1e-9);
        expect("morph target and amount", o.nodes[1].morphTarget && o.morphTargetScene == 1 && o.morphAmount == 0.35f);
        expect("return target", o.nodes[0].returnTarget);
        const auto m = buildOrbit(0, -1, -1, 24000.0, 0, 0.5f, -1, true);
        expect("morph onto the active scene is ignored", !m.nodes[0].morphTarget && m.morphAmount == 0.0f && m.previewing);
        expect("no queue, no countdown", m.countdownBeats < 0.0);
        const auto bad = buildOrbit(9, 7, 5, 0.0, 12, 2.0f, 8, false);
        expect("out-of-range indices are ignored", !bad.nodes[0].sounding && bad.countdownBeats < 0.0 && bad.morphAmount == 0.0f);
    }

    if (failures == 0) std::cout << "StageModelTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
