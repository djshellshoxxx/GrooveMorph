#include "core/scene/EditCoalescing.h"
#include "core/scene/SceneBank.h"
#include "core/scene/UndoStack.h"

#include <iostream>
#include <string_view>

using namespace groovemorph::core;
using namespace groovemorph::core::scene;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}

SceneBank withDepth(const SceneBank& bank, std::size_t scene, std::size_t op, float depth) {
    auto next = bank;
    auto settings = next.variation(scene).settings;
    settings.operations[op].depth = depth;
    next.setVariationSettings(scene, settings);
    return next;
}
} // namespace

int main() {
    // U1: field detection.
    {
        VariationSettings a;
        auto b = a;
        expect("U1 equal settings have no changed field", singleChangedNumericField(a, b) == 0);
        b.operations[1].depth = 0.3f;
        expect("U1 depth detected", singleChangedNumericField(a, b) == 3);
        b = a;
        b.operations[operationCount - 1].sliceBeats = 0.5f;
        expect("U1 slice of the last operation detected", singleChangedNumericField(a, b) == 2 + 2 * (operationCount - 1));
        b = a;
        b.operations[0].enabled = !b.operations[0].enabled;
        expect("U1 toggle is not numeric", singleChangedNumericField(a, b) == 0);
        b = a;
        b.operations[0].depth = 0.2f;
        b.operations[1].depth = 0.2f;
        expect("U1 two fields are not a single change", singleChangedNumericField(a, b) == 0);
        b = a;
        b.operations[0].depth = 0.2f;
        b.operations[0].enabled = !b.operations[0].enabled;
        expect("U1 numeric plus toggle is not a single change", singleChangedNumericField(a, b) == 0);
        b = a;
        b.control.placement.startPhase = 0.25f;
        expect("U1 placement field", singleChangedNumericField(a, b) == 101);
        b = a;
        b.control.phase.offsetBeats = 0.5f;
        expect("U1 phase offset", singleChangedNumericField(a, b) == 120);
        b = a;
        b.control.activity.maxCoverage = 0.5f;
        expect("U1 activity field", singleChangedNumericField(a, b) == 112);
        b = a;
        b.control.lane.explicitEvents = true;
        expect("U1 lane change is not numeric", singleChangedNumericField(a, b) == 0);
        auto d = a;
        d.operations[0].depth = 0.1f;
        expect("U1 key is non-zero and differs per scene", coalesceKey(0, a, d) != 0 && coalesceKey(0, a, d) != coalesceKey(1, a, d));
    }

    // U2: a run of steps on one field is one undo entry; undo returns to the pre-run state.
    {
        UndoStack stack;
        const SceneBank start = stack.current();
        auto bank = start;
        std::int64_t t = 1000;
        for (int i = 1; i <= 20; ++i) {
            bank = withDepth(bank, 0, 0, 0.05f * static_cast<float>(i));
            stack.commitCoalesced(bank, 42, t, 500);
            t += 100;
        }
        expect("U2 one entry for 20 steps", stack.undoDepth() == 1);
        expect("U2 newest value kept", stack.current() == bank);
        expect("U2 undo reverts the whole run", stack.undo() && stack.current() == start && !stack.canUndo());
        expect("U2 redo restores the newest value", stack.redo() && stack.current() == bank);
    }
    // U3: a pause longer than the window, a different key, or key 0 starts a new entry.
    {
        UndoStack stack;
        auto bank = stack.current();
        bank = withDepth(bank, 0, 0, 0.1f); stack.commitCoalesced(bank, 7, 0, 500);
        bank = withDepth(bank, 0, 0, 0.2f); stack.commitCoalesced(bank, 7, 500, 500);
        expect("U3 exactly at the window still merges", stack.undoDepth() == 1);
        bank = withDepth(bank, 0, 0, 0.3f); stack.commitCoalesced(bank, 7, 1001, 500);
        expect("U3 a gap larger than the window starts a new entry", stack.undoDepth() == 2);
        bank = withDepth(bank, 0, 1, 0.3f); stack.commitCoalesced(bank, 8, 1100, 500);
        expect("U3 a different key starts a new entry", stack.undoDepth() == 3);
        bank = withDepth(bank, 0, 1, 0.4f); stack.commitCoalesced(bank, 0, 1200, 500);
        bank = withDepth(bank, 0, 1, 0.5f); stack.commitCoalesced(bank, 0, 1250, 500);
        expect("U3 key 0 never merges", stack.undoDepth() == 5);
        // The window slides: steady stepping every 400 ms stays one entry for as long as it lasts.
        UndoStack slow;
        auto b2 = slow.current();
        for (int i = 1; i <= 10; ++i) {
            b2 = withDepth(b2, 0, 2, 0.05f * static_cast<float>(i));
            slow.commitCoalesced(b2, 9, 400 * i, 500);
        }
        expect("U3 sliding window", slow.undoDepth() == 1);
        // A clock that goes backwards never merges.
        UndoStack odd;
        auto b3 = withDepth(odd.current(), 0, 0, 0.1f); odd.commitCoalesced(b3, 5, 5000, 500);
        b3 = withDepth(b3, 0, 0, 0.2f); odd.commitCoalesced(b3, 5, 4000, 500);
        expect("U3 backwards clock does not merge", odd.undoDepth() == 2);
    }
    // U4: any ordinary commit, undo, redo or reset ends the run.
    {
        UndoStack stack;
        auto bank = withDepth(stack.current(), 0, 0, 0.1f);
        stack.commitCoalesced(bank, 3, 0, 500);
        auto other = bank;
        other.setVariationName(1, "x");
        stack.commit(other);
        bank = withDepth(other, 0, 0, 0.2f);
        stack.commitCoalesced(bank, 3, 100, 500);
        expect("U4 plain commit ends the run", stack.undoDepth() == 3);

        UndoStack u;
        auto b = withDepth(u.current(), 0, 0, 0.1f);
        u.commitCoalesced(b, 3, 0, 500);
        b = withDepth(b, 0, 0, 0.2f);
        u.commitCoalesced(b, 3, 100, 500);
        expect("U4 merged", u.undoDepth() == 1);
        expect("U4 undo", u.undo());
        expect("U4 redo", u.redo());
        auto b4 = withDepth(b, 0, 0, 0.3f);
        u.commitCoalesced(b4, 3, 150, 500);
        expect("U4 undo/redo ends the run", u.undoDepth() == 2);

        UndoStack r;
        auto b5 = withDepth(r.current(), 0, 0, 0.1f);
        r.commitCoalesced(b5, 3, 0, 500);
        r.reset(b5);
        b5 = withDepth(b5, 0, 0, 0.2f);
        r.commitCoalesced(b5, 3, 100, 500);
        expect("U4 reset ends the run and clears history", r.undoDepth() == 1);
    }
    // U5: a commit equal to the current state is a no-op and does not extend or break a run.
    {
        UndoStack stack;
        auto bank = withDepth(stack.current(), 0, 0, 0.1f);
        stack.commitCoalesced(bank, 11, 0, 500);
        stack.commitCoalesced(bank, 11, 100, 500);
        expect("U5 identical commit is a no-op", stack.undoDepth() == 1);
    }
    // U6: capacity is respected when entries are not merged.
    {
        UndoStack stack(SceneBank{}, 4);
        auto bank = stack.current();
        for (int i = 1; i <= 10; ++i) {
            bank = withDepth(bank, 0, 0, 0.05f * static_cast<float>(i));
            stack.commitCoalesced(bank, 0, 1000 * i, 500);
        }
        expect("U6 bounded history", stack.undoDepth() == 4);
    }

    if (failures == 0) std::cout << "EditCoalescingTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
