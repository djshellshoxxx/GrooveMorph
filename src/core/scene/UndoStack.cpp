#include "core/scene/UndoStack.h"

#include <utility>

namespace groovemorph::core::scene
{
UndoStack::UndoStack(SceneBank initial, std::size_t capacity)
    : state(std::move(initial)), maxDepth(capacity == 0 ? 1 : capacity)
{
}

void UndoStack::commit(const SceneBank& next)
{
    if (next == state)
        return;
    undoHistory.push_back(std::move(state));
    if (undoHistory.size() > maxDepth)
        undoHistory.pop_front();
    state = next;
    redoHistory.clear();
}

bool UndoStack::undo()
{
    if (undoHistory.empty())
        return false;
    redoHistory.push_back(std::move(state));
    state = std::move(undoHistory.back());
    undoHistory.pop_back();
    return true;
}

bool UndoStack::redo()
{
    if (redoHistory.empty())
        return false;
    undoHistory.push_back(std::move(state));
    state = std::move(redoHistory.back());
    redoHistory.pop_back();
    return true;
}

void UndoStack::reset(const SceneBank& s)
{
    state = s;
    undoHistory.clear();
    redoHistory.clear();
}
} // namespace groovemorph::core::scene
