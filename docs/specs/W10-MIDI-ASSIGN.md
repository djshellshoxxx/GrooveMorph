# W10-MIDI-ASSIGN: scene note assignment and MIDI learn (A04)

Status: implemented by W10. W1's parallel A04 policy spec, if it disagrees, loses to this one for the
code in this branch; the integrator should reconcile any difference (W10's final report lists what was kept).

## Objective

1. A MIDI note can no longer be given to a second scene silently. The user sees who owns the note and
   chooses; the safe choice (do nothing) is the default.
2. MIDI learn picks the controller the user actually moved, tells the user when it cannot link, and no
   longer loses fast pulses between two polls.

Non-goals: changing which scene wins when a legacy project already holds a duplicate (first match
stays); changing the number of scenes; per-channel note maps; learn for notes.

## Design choice: Swap with confirmation, not Replace

Options considered for "scene B wants note 61, scene C owns it":

| Option | Result | Verdict |
|---|---|---|
| Keep first match (today) | B and C share 61, only B fires, C is silently dead | rejected: the bug |
| Reject | Nothing changes, user must free the note first | safe but annoying, still offered (Cancel) |
| Replace (C loses its note) | C is unmapped or gets an invented note | rejected: removes a mapping the user did not touch |
| Swap (C gets B's old note) | Every scene keeps a distinct note | chosen |

Swap never creates a duplicate and never invents a note. It needs a confirmation because it changes a
scene the user was not editing.

## Core (`src/core/scene/MidiNoteMap.*`)

- `ownerOf(note, except = variationCount)`: first scene other than `except` mapped to the note.
- `planAssign(index, note)` returns `NoteAssignPlan{kind, owner, ownerNewNote}`; kinds `Invalid`
  (index or note out of 0..127), `Unchanged`, `Free`, `Conflict`. `ownerNewNote` is the note `index`
  gives up. Pure and const.
- `assign(index, note, choice)`: `Free` sets the note. `Conflict` with `ConflictChoice::Swap` sets
  `notes[owner] = ownerNewNote` and `notes[index] = note`; with `Cancel` (the default) nothing changes.
  Returns true only when the map changed.
- Dispatch (`variationForNote`) is untouched: first mapped scene wins. A legacy duplicate is still
  reported by `firstConflictingNote()` and by the editor's existing MIDI conflict label.
- If a legacy duplicate holds the requested note, only the first owner is swapped. No new duplicate of
  the displaced note is created.

State and serialization: none. The map is already part of the scene file; saved maps (with or without
duplicates) load and play exactly as before. Nothing is migrated.

## UI wiring (`PluginEditor.cpp`, `editMidiMap`)

- The note slider/field still calls `editMidiMap()`. It now calls `planAssign` first.
- `Free`/`Unchanged`: apply as before (one undoable `setMidiNoteMap`).
- `Conflict`: a non-blocking `AlertWindow::showAsync` (same pattern as `confirmReset`): title "Note N is
  already used", text "Note N (name) triggers scene X (its name). Swap so scene X gets note M, or cancel."
  Buttons "Swap" / "Cancel". Swap applies `assign(..., Swap)` through `setMidiNoteMap`, which is one
  undo step (the existing scene Undo button reverts both notes together). Cancel puts the slider back
  to the stored value.
- Only one dialog at a time; the slider is disabled while it is open. A preview in progress still blocks
  map edits as before.
- The conflict label for legacy duplicates stays and its tooltip now says how to fix it ("set one of
  them again; you will be offered a swap").

## MIDI learn

- `CcInbox` (audio thread -> message thread mailbox, atomics only, written from `processBlock`) now keeps
  a per-CC `travel` counter: the sum of |step| of every CC event. `collect()` reports a CC when its
  newest value differs OR its travel changed since the last collect, so 0 -> 127 -> 0 between two polls is
  no longer lost. `Change.travel` is the movement since the last collect. A repeated identical value adds
  no travel and is not reported (no host-parameter flooding).
- The audio thread writes the mailbox for every CC event of every block; the message thread reads it at
  the processor timer rate (20 Hz). Applying a linked value is rate limited by that timer (see
  W10-MIDI-PANIC-RATES.md); learning no longer depends on seeing the final value.
- `pickLearnCc(changes, n)`: among the CCs reported by one collect, the one with the largest travel
  wins, ties go to the lowest CC, zero travel is never picked. This replaces "lowest-numbered CC that
  moved".
- `MidiLinkTable::linkChecked` returns `Ok`, `Invalid`, `TableFull`. `link()` keeps its bool contract.
- When learn finishes with `TableFull` the learn state is left (no endless armed outline) and the
  processor publishes a notice code. The editor shows the sentence "MIDI link table full (64 links).
  Unlink a control (right-click > Unlink) to free a slot." in the status line for 8 s and once in an
  async message box. Relinking a target that already has a link still works when the table is full.
- The right-click menu shows "Link to MIDI controller (table full, 64)" disabled when the table is full
  and the control has no link.
- Known limit kept: `s:` scene links are ignored while a preview is active (documented in help).

## Acceptance tests

| ID | Check | Where |
|---|---|---|
| A04-01 | `ownerOf` finds the owner, ignores `except`, rejects invalid notes | `MidiNoteAssignTests` |
| A04-02 | `planAssign` classifies Unchanged/Free/Conflict/Invalid and is const | `MidiNoteAssignTests` |
| A04-03 | `assign` Free works; Cancel and invalid change nothing; Swap exchanges and leaves no duplicate | `MidiNoteAssignTests` |
| A04-04 | Legacy duplicate maps keep first-match dispatch, round trip through the bank, swap touches the first owner only | `MidiNoteAssignTests` |
| A04-05 | `releaseAll` behaves as the held note's note-off (see PANIC spec) | `MidiNoteAssignTests` |
| A04-06 | A pulse that returns to its value is reported; repeats are not; learn picks the biggest mover; ties lowest; zero travel never; discard forgets travel | `MidiLinksTests` |
| A04-07 | `TableFull` vs `Invalid`; relink while full works; `p:opNDepth` targets are valid | `MidiLinksTests` |
| A04-08 | Processor: assigning through `setMidiNoteMap` after a swap is one undo step; saved duplicate map loads unchanged | `PluginProcessorTests` (processor test binary; ran green locally 2026-10-20, see the verification note in W10-UI-ISSUES.md) |
| A04-09 | Processor: learn on a full table reports the notice and disarms; learn picks the larger mover | `PluginProcessorTests` (processor test binary; ran green locally 2026-10-20, see the verification note in W10-UI-ISSUES.md) |
| A04-10 | Swap dialog, slider reset on Cancel, table-full message, menu item | manual (not verifiable headless; needs a display) |
