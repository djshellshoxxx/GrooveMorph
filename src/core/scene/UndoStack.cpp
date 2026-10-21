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
    runKey = 0;
    undoHistory.push_back(std::move(state));
    if (undoHistory.size() > maxDepth)
        undoHistory.pop_front();
    state = next;
    redoHistory.clear();
}

void UndoStack::commitCoalesced(const SceneBank& next, std::uint64_t key, std::int64_t nowMs,
                                std::int64_t windowMs)
{
    if (next == state)
        return;
    const bool merge = key != 0 && key == runKey && !undoHistory.empty() && nowMs >= runLastMs
                       && nowMs - runLastMs <= windowMs;
    if (merge)
    {
        state = next; // the run's first commit already holds the pre-run state; redo is already empty
        runLastMs = nowMs;
        return;
    }
    commit(next);
    runKey = key;
    runLastMs = nowMs;
}

bool UndoStack::undo()
{
    runKey = 0;
    if (undoHistory.empty())
        return false;
    redoHistory.push_back(std::move(state));
    state = std::move(undoHistory.back());
    undoHistory.pop_back();
    return true;
}

bool UndoStack::redo()
{
    runKey = 0;
    if (redoHistory.empty())
        return false;
    undoHistory.push_back(std::move(state));
    state = std::move(redoHistory.back());
    redoHistory.pop_back();
    return true;
}

void UndoStack::reset(const SceneBank& s)
{
    runKey = 0;
    state = s;
    undoHistory.clear();
    redoHistory.clear();
}
} // namespace groovemorph::core::scene
