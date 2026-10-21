# A04 MIDI duplicate-assignment policy

Status: IMPLEMENTED as the W10 variant (Swap/Cancel dialog); CC reserved-slot policy not built (TODO B-05). Updated 2026-10-20 by the A1 completeness audit.
Source item: `docs/TODO.md` A04 ("Decide whether to add auto-swap/reject"). Related: [F12](F12-MIDI-CLAP-PLATFORMS.md), [F04](F04-PERFORMANCE-MODE.md).


## Beta audit status (2026-10-20)

- The decision in section 3 (silent auto-swap with an undoable notice) was NOT what shipped. W10 chose a confirmation dialog with Cancel as the default and no Fix button; see `docs/specs/W10-MIDI-ASSIGN.md` (the as-built spec). A legacy duplicate map still loads unchanged and dispatches first-match.
- Rows A04-T03..T06 are re-pointed at the as-built tests in `MidiNoteAssignTests.cpp` and `PluginProcessorTests.cpp`. There is no `resolveDuplicates()`.
- Not built: CC 20/21/22 rejection while macros are on, macro-slot skipping, shared-CC description (A04-T07..T09, B-05). A link on CC 20/21/22 and the macro both drive their targets today.
- Acceptance rows below carry audited statuses; the full join of ID, test, implementation and UI wiring is in [COMPLETENESS-MATRIX.md](COMPLETENESS-MATRIX.md).

## 1. Objective

No silent MIDI collisions: every way two controls can claim the same MIDI input is either prevented, resolved with an undoable edit, or shown to the user.

## 2. Current behavior (facts)

1. Scene notes: `MidiNoteMap::notes[4]`, defaults 60, 61, 62, 63; clamped 0..127. `variationForNote` returns the FIRST variation with the note, so duplicates silently shadow
   later scenes. `firstConflictingNote()` only diagnoses; the editor shows a "MIDI conflict: note N maps to multiple variations" label and tooltip (`PluginEditor.cpp`). Project files with duplicates load unchanged
   (`SceneSerializerTests` "duplicate notes load unchanged", "duplicate notes are mentioned").
2. CC links: `MidiLinkTable` allows one CC to drive several targets (test "one CC may drive two targets") and replaces the CC of a target on relink. Learn takes the first CC that moves and does not check other links.
3. CC macros (`midiMacros` on): CC 20/21/22 and program change are handled in `handleMidi`. `ccInbox.push` runs BEFORE the macro branch, so a link on CC 20 AND the macro both drive their targets.
4. A CC used as a link can also be the CC another instance/hardware uses; not detectable by the plugin and out of scope.

## 3. Decision (the original recommendation; NOT what shipped, see the Beta audit status above and W10-MIDI-ASSIGN)

Auto-swap for scene notes, with a non-modal, undoable notice; reject for reserved CCs. Rationale: scene notes are a permutation-like assignment of 4 scenes; rejecting forces a three-step dance (move
A away, set B, set A) that is awkward when the player is learning a pad layout; swapping always leaves every scene reachable. A modal confirmation blocks a live performer, so the "confirmation" is an undoable
notice. Rejecting reserved CCs is correct because a macro/link double-drive can never be intended.

Alternative if the owner prefers least surprise: reject duplicates entirely (revert the control, show the message). Record the choice in `docs/coordination/DECISIONS.md` (W10 writes the entry).

## 4. Required behavior (original draft; only the note-collision part shipped, as a Swap/Cancel dialog)

### 4.1 Scene notes (`SceneBank::setMidiNoteMap` path, editor note slider)
- Setting scene X to note N where scene Y (Y != X) already has N: set Y to X's previous note (swap). One undoable scene edit (single `commitBankLocked`; Undo restores both notes).
  The editor shows a status line for 5 s: "Note N moved from Y to X; Y now uses M. Undo to revert." with the existing Undo button enabled.
- If X's previous note is also not unique after the swap (legacy duplicates), choose the lowest unused note at or above X's previous note (wrap at 127); deterministic.
- Loading a project with duplicates does NOT modify it; the conflict label stays and gains a "Fix" button that applies the same swap logic to produce a duplicate-free map as ONE undoable edit
  (order: scenes A to D, each later duplicate moves to the lowest unused note >= its current note).
- `NoteOffPolicy` is unchanged by swaps.
- Core API (portable, no JUCE): `MidiNoteMap::assignNote(std::size_t scene, int note)` returning `{changedScene, swappedScene, swappedNote}`; `MidiNoteMap::resolveDuplicates()`.

### 4.2 CC links
- A CC may keep driving several targets (existing, documented, tested). The control menu and the learn status line name the sharing: "CC 21 also drives: Dry/Wet".
- Reserved CCs: 20, 21, 22 are reserved for macros WHILE `midiMacros` is on. `MidiLinkTable::link` (or the processor wrapper `linkMidi`) rejects them with the reason
  "CC 20 is used by MIDI macros" and learn ignores them; the user switches macros off to relink them. Enabling macros while links exist on 20-22 is allowed; the explicit LINK wins
  and the macro slot for that CC is skipped in `applyMidiMacros`, with the Input tab showing "CC 20 is linked and overrides the macro".
- Program change is not a CC and has no conflict with links.
- Conflict badge (owned by W10): a control with a shared or overridden CC shows a small badge using the existing `paintMidiOverlay`; tooltip text lists the other controls.

### 4.3 Persistence
No new state keys. Note map keeps `midi.note.<i>`. Links keep the `MidiLinks` serialization; rejected links are never written.

## 5. Real-time and threading

All resolution runs on the message thread. The audio thread continues to read the published `MidiNoteMap` snapshot unchanged (first match still applies, but the map is now duplicate-free for new edits).

## 6. UI wiring

Editor: note slider per scene (existing), status/notice line (existing `midiLabel`), new "Fix" button next to the conflict label. Input tab: macro override note. Right-click menu: sharing information.

## 7. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| A04-T01 | Collision diagnostics: duplicate, nonadjacent, first reported deterministically, out-of-range ignored, cleared by edit | EXISTS | `tests/core/SceneBankTests.cpp` "detect colliding MIDI note", "detect nonadjacent collision", "report first collision deterministically", "ignore out-of-range note collisions", "clearing collision removes warning" |
| A04-T02 | Legacy project with duplicates loads unchanged and is mentioned | EXISTS | `tests/core/SceneSerializerTests.cpp` "duplicate notes load unchanged", "duplicate notes are mentioned" |
| A04-T03 | `assign(index, note, Swap)` swaps with the owner of the note; result duplicate-free | EXISTS | `MidiNoteAssignTests.cpp` "A04-03 swap"; `MidiNoteAssignTests.cpp` "A04-03 swap result has no conflict" |
| A04-T04 | Swap is one undo step and Undo restores both notes | EXISTS | `PluginProcessorTests.cpp` "A04-08 one undo reverts both notes"; `PluginProcessorTests.cpp` "A04-08 map swapped in the bank" |
| A04-T05 | Legacy duplicate maps keep first-match dispatch, and a swap touches only the first owner (there is no `resolveDuplicates`) | EXISTS | `MidiNoteAssignTests.cpp` "A04-04 legacy dispatch is first-match"; `MidiNoteAssignTests.cpp` "A04-04 swap takes first owner only" As built there is no resolveDuplicates(): legacy duplicate maps are left alone (first match) and fixed by assigning again; see the rewritten spec. |
| A04-T06 | Assigning a note already unique to the scene is a no-op (no undo entry) | EXISTS | `MidiNoteAssignTests.cpp` "A04-03 same note is not a change"; `MidiNoteAssignTests.cpp` "A04-02 unchanged" |
| A04-T07 | Linking CC 20/21/22 is rejected while macros are on, accepted when off; learn ignores them | DEFERRED | Not built: linking CC 20/21/22 is still allowed while macros are on (the link and the macro both drive their targets). Open as TODO B-05. |
| A04-T08 | Macro slot skipped when a link uses the same CC (no double drive) | DEFERRED | Not built (same as A04-T07). Open as TODO B-05. |
| A04-T09 | Shared CC description lists all targets | DEFERRED | Not built: the right-click menu does not list other targets that share a CC. Open as TODO B-05. |
| A04-T10 | Swap/Cancel dialog, table-full notice and slider reset exist (native UI; the Fix button was not built) | MANUAL | The Swap/Cancel dialog replaced the planned Fix button; UI code is compiled in CI only and has not been clicked (W10-MIDI-ASSIGN A04-10). |

## 8. Non-goals

Per-channel MIDI maps, detecting collisions with other plugins, MIDI clock/transport mapping, changing the first-match rule for saved projects.
