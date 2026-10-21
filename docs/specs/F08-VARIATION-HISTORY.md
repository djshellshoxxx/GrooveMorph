# F08 Generation history and recall

Status: IMPLEMENTED (local candidate history; codec in core). Beta audit: embedding in the project is a draft requirement, not built (TODO B-08). Updated 2026-10-20 by the A1 completeness audit.
Replaces draft `docs/expansion/features/F08-VARIATION-HISTORY.md` (PR #6). The draft's branching revision graph and project-embedded
history are NOT implemented. Scene undo/redo (separate mechanism) is documented here for clarity.


## Beta audit status (2026-10-20)

- Built by the audit: the history file codec moved from `InspectorPanel.cpp` to `src/core/variation/GeneratorHistory.{h,cpp}` with the same format (`groovemorph-generator-history 2`), tested by `tests/core/GeneratorHistoryTests.cpp` (F08-T04..T07).
- Open: history inside the DAW project (F08-T08, B-08). The decision to keep it a local file stands until the owner asks.
- Acceptance rows below carry audited statuses; the full join of ID, test, implementation and UI wiring is in [COMPLETENESS-MATRIX.md](COMPLETENESS-MATRIX.md).

## 1. Objective

Remember the last generated candidates so any of them can be recalled EXACTLY (same sound), and make every scene edit undoable.

## 2. As implemented

### 2.1 Scene undo/redo (project state)
`core::scene::UndoStack` (`src/core/scene/UndoStack.{h,cpp}`): bounded snapshots of the whole `SceneBank`; `commit`, `amend` (used by live
controller edits, no new entry), `undo`, `redo`, new edit clears redo. Processor wrappers `undoSceneEdit/redoSceneEdit/canUndo...`.
Audition (`AuditionSession`) holds an editable copy; `commitAudition()` is ONE undoable edit; `revertAudition()` leaves the bank.
Undo is refused during audition. `setStateInformation` resets the stack.

### 2.2 Generator history (local file, NOT project state)
`GeneratorTab` (`src/ui/InspectorPanel.cpp`, `savePersistentHistory/loadPersistentHistory`) keeps at most 24 entries. File:
`<userApplicationDataDirectory>/GrooveMorph/generator-history.txt`, header `groovemorph-generator-history 2`. Per entry a line
`entry <scene> <seed> <style 0..2> <distance> <candidate> <protectedOperations> <generateEvents 0|1> <phraseTicks>` followed by the source scene
serialized with `SceneSerializer` (one-variation bank) and `end-entry`. Loader limits: file <= 1 MiB, rejects bad headers, unknown style,
non-0/1 flag, `phraseTicks <= 0`, non-finite distance; a source whose text yields `LoadStatus::Defaulted` is skipped; stops at the first
malformed header. Selecting an entry regenerates it with `buildCandidate(entry.source, entry.settings)` and previews it (nothing committed until Apply).
Because candidates are pure functions (F01), only the inputs are stored, never the result.

## 3. Parameters

None.

## 4. State

Scene undo is session-only (not saved). Generator history is per computer, not embedded in the DAW project, and is shared by all plugin instances on
that computer (last writer wins; no locking). `replaceWithText` writes atomically-enough for a single instance; concurrent instances can lose entries
(KNOWN LIMITATION).

## 5. Real-time and threading

Message thread only. File I/O happens on the message thread when a candidate is generated or the tab is built; it never occurs in the audio path.
Owner W10: confirm no file I/O happens under `modelMutex`.

## 6. UI wiring

Gen tab: "Generation history" combo (disabled when empty), entry text "#N scene X ...". Editor: Undo/Redo buttons, audition Apply/Revert/Compare.

## 7. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F08-T01 | Undo depth, redo, new edit clears redo, bounded | EXISTS | `tests/core/SceneUndoAuditionTests.cpp` "depth 2", "undo to b0", "redo to b1", "new edit clears redo", "bounded undo steps" |
| F08-T02 | Audition commit is one undo step; revert leaves the bank | EXISTS | SceneUndoAuditionTests "commit undoable", "revert leaves bank"; PluginProcessorTests "undo refused during audition" |
| F08-T03 | Any candidate is exactly reproducible from stored inputs | EXISTS (builder determinism) | CandidateBuilderTests "same inputs same candidate" |
| F08-T04 | History file round trip: write 24 entries, reload, recall equals original candidate | EXISTS | `GeneratorHistoryTests.cpp (label F08-T04)` |
| F08-T05 | Hostile history file (huge, truncated, wrong header, NaN) is rejected without crash | EXISTS | `GeneratorHistoryTests.cpp (label F08-T05)` |
| F08-T06 | 25th entry evicts the oldest; order stable | EXISTS | `GeneratorHistoryTests.cpp (label F08-T06)` |
| F08-T07 | Legacy history version (`... 1`) is ignored, not misparsed | EXISTS | `GeneratorHistoryTests.cpp (label F08-T07)` |
| F08-T08 | Project-embedded history (draft requirement) | NOT PLANNED for Beta; record as a decision if the owner wants it | n/a |

## 8. Gaps

Unowned: extract the history codec to core for testability (T04-T07); no branching graph; no per-entry audio audition simultaneously.
Depends on the scene format staying `v1`; any scene-key change by W3 (capacity 256, regions 8) must keep old history files loadable.

## 9. Non-goals

Embedding history in the DAW project, cloud sync, branching and merge UI.
