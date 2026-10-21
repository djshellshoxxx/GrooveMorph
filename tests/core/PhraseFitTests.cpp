#include "core/control/PhraseFit.h"

#include <iostream>
#include <string_view>

using namespace groovemorph::core::control;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}
LaneEvent event(std::uint8_t op, int onset, int duration) {
    LaneEvent e;
    e.operation = op;
    e.onsetTicks = onset;
    e.durationTicks = duration;
    return e;
}
} // namespace

int main() {
    constexpr std::int64_t bar = 4 * ticksPerBeat;
    // F1: uniform lanes have nothing to check; an empty explicit lane fits.
    {
        EventLane lane;
        expect("F1 uniform", !checkPhraseFit(lane, 4 * bar).conflict());
        lane.explicitEvents = true;
        expect("F1 empty explicit", !checkPhraseFit(lane, 4 * bar).conflict());
    }
    // F2: events inside the phrase fit; events starting past a shortened phrase or crossing its end are counted.
    {
        EventLane lane;
        lane.explicitEvents = true;
        (void) addEvent(lane, event(0, 0, ticksPerBeat), 4 * bar);            // fits in 4 bars and in 1 bar
        (void) addEvent(lane, event(1, 2 * bar, ticksPerBeat), 4 * bar);      // past a 1-bar phrase
        (void) addEvent(lane, event(2, bar - 480, 960), 4 * bar);             // crosses the end of a 1-bar phrase
        expect("F2 fits the original phrase", !checkPhraseFit(lane, 4 * bar).conflict());
        const auto shortened = checkPhraseFit(lane, bar);
        expect("F2 shortened phrase", shortened.conflict() && shortened.pastEnd == 1 && shortened.overhang == 1 && shortened.affected() == 2);
        EventLane exact;
        exact.explicitEvents = true;
        (void) addEvent(exact, event(0, bar - ticksPerBeat, ticksPerBeat), 4 * bar);
        expect("F2 event ending exactly at the end fits", !checkPhraseFit(exact, bar).conflict());
        expect("F2 invalid phrase length", !checkPhraseFit(lane, 0).conflict() && !checkPhraseFit(lane, -5).conflict());
    }
    // F3: locked events are reported so the badge can say they will not be moved by the Scale command.
    {
        EventLane lane;
        lane.explicitEvents = true;
        auto locked = event(0, 3 * bar, ticksPerBeat);
        locked.locked = true;
        (void) addEvent(lane, locked, 4 * bar);
        (void) addEvent(lane, event(1, 3 * bar, ticksPerBeat), 4 * bar);
        PermissionConfig permissions;
        permissions.operationLocks[1] = lock::events;
        const auto report = checkPhraseFit(lane, bar, &permissions);
        expect("F3 locked counted", report.pastEnd == 2 && report.lockedAffected == 2);
        expect("F3 without permissions only per-event locks count", checkPhraseFit(lane, bar).lockedAffected == 1);
    }
    // F4: a polyrhythm loop ring is the reference for its operation.
    {
        EventLane lane;
        lane.explicitEvents = true;
        lane.loopBeats[0] = 3; // ring of 3 beats
        (void) addEvent(lane, event(0, 0, ticksPerBeat), 4 * bar);
        expect("F4 fits its ring", !checkPhraseFit(lane, 4 * bar).conflict());
        lane.events[0].durationTicks = 4 * ticksPerBeat; // longer than the 3 beat ring
        expect("F4 overhangs its ring", checkPhraseFit(lane, 4 * bar).overhang == 1);
    }

    if (failures == 0) std::cout << "PhraseFitTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
