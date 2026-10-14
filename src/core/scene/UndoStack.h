#pragma once

#include "core/scene/SceneBank.h"

#include <cstddef>
#include <deque>

namespace groovemorph::core::scene
{
// Bounded undo/redo history of whole-bank snapshots (GM-FR-009). Message thread only.
class UndoStack final
{
public:
    explicit UndoStack(SceneBank initial = {}, std::size_t capacity = 64);

    [[nodiscard]] const SceneBank& current() const noexcept { return state; }
    // Records the current state for undo, makes `next` current and clears redo.
    // A commit equal to the current state is a no-op.
    void commit(const SceneBank& next);
    // Replaces the current state WITHOUT recording history (live performance moves such as MIDI controllers,
    // which would otherwise flood the undo list). Redo history is left alone.
    void amend(const SceneBank& next) { state = next; }
    bool undo(); // false when nothing to undo
    bool redo(); // false when nothing to redo
    void reset(const SceneBank& state); // clears all history

    [[nodiscard]] bool canUndo() const noexcept { return !undoHistory.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redoHistory.empty(); }
    [[nodiscard]] std::size_t undoDepth() const noexcept { return undoHistory.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return maxDepth; }

private:
    SceneBank state;
    std::deque<SceneBank> undoHistory;
    std::deque<SceneBank> redoHistory;
    std::size_t maxDepth;
};
} // namespace groovemorph::core::scene
