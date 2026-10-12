#include "core/scene/AuditionSession.h"
#include "core/scene/UndoStack.h"

#include <cstdint>
#include <iostream>
#include <string_view>

using namespace groovemorph::core;
using namespace groovemorph::core::scene;

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
} // namespace

int main()
{
    // Undo/redo
    const SceneBank b0;
    UndoStack undo(b0, 3);
    expect("empty history", !undo.canUndo() && !undo.canRedo() && !undo.undo() && !undo.redo());
    SceneBank b1 = b0;
    b1.setActiveIndex(1);
    SceneBank b2 = b1;
    b2.setVariationName(2, "Edited");
    undo.commit(b1);
    undo.commit(b2);
    undo.commit(b2); // no-op
    expect("depth 2", undo.undoDepth() == 2);
    expect("undo to b1", undo.undo() && undo.current() == b1);
    expect("undo to b0", undo.undo() && undo.current() == b0 && !undo.canUndo());
    expect("redo to b1", undo.redo() && undo.current() == b1);
    SceneBank b3 = b1;
    b3.setSeed(77);
    undo.commit(b3);
    expect("new edit clears redo", !undo.canRedo() && undo.current() == b3);
    for (int i = 0; i < 10; ++i)
    {
        SceneBank n = undo.current();
        n.setSeed(static_cast<std::uint64_t>(100 + i));
        undo.commit(n);
    }
    expect("bounded", undo.undoDepth() == 3);
    int steps = 0;
    while (undo.undo())
        ++steps;
    expect("bounded undo steps", steps == 3 && undo.current().seed() == 106);
    undo.reset(b0);
    expect("reset", undo.current() == b0 && !undo.canUndo() && !undo.canRedo());

    // Audition / compare
    SceneBank bank;
    const SceneBank persisted = bank;
    AuditionSession session;
    expect("inactive snapshot", !session.isActive() && !session.commit(bank));
    session.begin(bank, 2);
    Variation v = session.edited();
    v.settings[OperationType::Gate].enabled = true;
    v.name = "C Gated";
    session.setEdited(v);
    expect("dirty", session.isDirty() && session.audible() == session.edited());
    expect("bank untouched by audition", bank == persisted);
    auto snap = session.audibleSnapshot(bank);
    expect("audition snapshot uses edit", snap.activeIndex == 2
                                              && snap.variations[2][OperationType::Gate].enabled);
    session.toggleCompare();
    expect("compare hears stored", session.comparingStored()
                                       && session.audible() == persisted.variation(2));
    snap = session.audibleSnapshot(bank);
    expect("compare snapshot stored", !snap.variations[2][OperationType::Gate].enabled);
    session.toggleCompare();
    expect("compare back to edit", session.audible() == session.edited());
    session.revert();
    expect("revert leaves bank", !session.isActive() && bank == persisted);
    expect("inactive snapshot = bank", bank.snapshot().activeIndex
                                           == session.audibleSnapshot(bank).activeIndex);

    session.begin(bank, 2);
    session.setEdited(v);
    UndoStack history(bank);
    SceneBank next = history.current();
    expect("commit", session.commit(next) && !session.isActive());
    history.commit(next);
    expect("committed into bank", history.current().variation(2).name == "C Gated"
                                      && history.current().variation(1) == persisted.variation(1));
    expect("commit undoable", history.undo() && history.current() == persisted);

    if (failures == 0)
        std::cout << "SceneUndoAuditionTests passed\n";
    return failures == 0 ? 0 : 1;
}
