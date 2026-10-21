#pragma once

#include "core/scene/SceneBank.h"

#include <cstddef>
#include <cstdint>
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
    // Like commit(), but successive commits with the same non-zero `key` whose timestamps are at most
    // `windowMs` apart merge into the undo entry created by the first one (one undo step reverts the
    // whole run). Any other commit, undo, redo or reset ends the run. key 0 never merges. `nowMs` is a
    // caller-supplied monotonic millisecond clock, so the logic stays portable and testable.
    void commitCoalesced(const SceneBank& next, std::uint64_t key, std::int64_t nowMs, std::int64_t windowMs = 500);
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
    std::uint64_t runKey = 0;   // key of the open coalescing run; 0 = none
    std::int64_t runLastMs = 0; // timestamp of the newest commit of the run
};
} // namespace groovemorph::core::scene
