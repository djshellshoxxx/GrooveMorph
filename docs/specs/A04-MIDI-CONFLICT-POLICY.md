# A04 MIDI duplicate-assignment policy

Status: SPEC COMPLETE, decision RECOMMENDED (owner to confirm), implementation owned by W10. Written 2026-10-20 against `c83e8b2`.
Source item: `docs/TODO.md` A04 ("Decide whether to add auto-swap/reject"). Related: [F12](F12-MIDI-CLAP-PLATFORMS.md), [F04](F04-PERFORMANCE-MODE.md).

## 1. Objective

No silent MIDI collisions: every way two controls can claim the same MIDI input is either prevented, resolved with an undoable edit, or shown to the user.

## 2. Current behavior (facts)

1. Scene notes: `MidiNoteMap::notes[4]`, defaults 60, 61, 62, 63; clamped 0..127. `variationForNote` returns the FIRST variation with the note, so duplicates silently shadow
   later scenes. `firstConflictingNote()` only diagnoses; the editor shows a "MIDI conflict: note N maps to multiple variations" label and tooltip (`PluginEditor.cpp`). Project files with duplicates load unchanged
   (`SceneSerializerTests` "duplicate notes load unchanged", "duplicate notes are mentioned").
2. CC links: `MidiLinkTable` allows one CC to drive several targets (test "one CC may drive two targets") and replaces the CC of a target on relink. Learn takes the first CC that moves and does not check other links.
3. CC macros (`midiMacros` on): CC 20/21/22 and program change are handled in `handleMidi`. `ccInbox.push` runs BEFORE the macro branch, so a link on CC 20 AND the macro both drive their targets.
4. A CC used as a link can also be the CC another instance/hardware uses; not detectable by the plugin and out of scope.

## 3. Decision (recommended)

Auto-swap for scene notes, with a non-modal, undoable notice; reject for reserved CCs. Rationale: scene notes are a permutation-like assignment of 4 scenes; rejecting forces a three-step dance (move
A away, set B, set A) that is awkward when the player is learning a pad layout; swapping always leaves every scene reachable. A modal confirmation blocks a live performer, so the "confirmation" is an undoable
notice. Rejecting reserved CCs is correct because a macro/link double-drive can never be intended.

Alternative if the owner prefers least surprise: reject duplicates entirely (revert the control, show the message). Record the choice in `docs/coordination/DECISIONS.md` (W10 writes the entry).

## 4. Required behavior

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
| A04-T03 | `assignNote` swaps with the owner of the note; result duplicate-free | MISSING, owned by W10 | `tests/core/SceneBankTests.cpp` |
| A04-T04 | Swap is one undo step and Undo restores both notes | MISSING, owned by W10 | `tests/PluginProcessorTests.cpp` |
| A04-T05 | `resolveDuplicates` is deterministic and terminates for all 128-note edge cases (all four on 127; all four on 0) | MISSING, owned by W10 | SceneBankTests |
| A04-T06 | Assigning a note already unique to the scene is a no-op (no undo entry) | MISSING, owned by W10 | SceneBankTests |
| A04-T07 | Linking CC 20/21/22 is rejected while macros are on, accepted when off; learn ignores them | MISSING, owned by W10 | MidiLinksTests + processor test |
| A04-T08 | Macro slot skipped when a link uses the same CC (no double drive) | MISSING, owned by W10 | processor test |
| A04-T09 | Shared CC description lists all targets | MISSING, owned by W10 | MidiLinksTests |
| A04-T10 | Notice text and "Fix" button exist and update (native UI) | MISSING (needs JUCE build) | editor test on CI |

## 8. Non-goals

Per-channel MIDI maps, detecting collisions with other plugins, MIDI clock/transport mapping, changing the first-match rule for saved projects.
