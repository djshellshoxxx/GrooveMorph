# W10-UI-ISSUES: known editor issues closed in the beta (lane keyboard, handles, badge, honesty, undo, accessibility, rates)

Status: implemented by W10. Verification, stated exactly:
- Portable core tests (`ctest`, 34 targets) pass.
- The whole editor/processor code was compiled (`-fsyntax-only`, then `-O1` objects) against JUCE 8.0.7 headers on the authoring
  machine, and `tests/PluginProcessorTests.cpp` (including the new W10 blocks and the new accessible-name check
  `UI-AX-1`, which constructs the real editor) was linked against JUCE module objects from another worker's build
  archive and run green. That is a headless run: it exercises processor logic and builds the editor, but nobody
  clicked or typed in a window, and no screen reader was used.
- Rows marked "manual" below are therefore UNVERIFIED behavior: keyboard/mouse interaction of the lane canvas,
  the swap dialog, the badge drawing, the Scenes tab texts, and screen-reader output.

Shared-hotspot rule: edits in `InspectorPanel.cpp`, `PluginEditor.*`, `ControlMenu.*` are small and
localized. New logic lives in `src/core/` (tested) or tiny new headers.

## U1 Event creation by keyboard (`LaneCanvas`)

The canvas already takes keyboard focus, but only an existing selection reacted to keys, so events
could not be created without a mouse.

Keyboard cursor: a lane (0..laneCount-1) and a tick position, drawn as a short outlined marker while the
canvas has focus and no event is selected. Initial position: lane 0, tick 0.

| Key (no event selected) | Action |
|---|---|
| Left / Right | move the cursor by one snap unit (Shift: one beat), clamped to the phrase |
| Up / Down | change the cursor lane |
| Enter, Insert or N | create an event of one snap unit at the cursor in that lane, select it, move the cursor to its end |
| Tab-focus away | unchanged |

| Key (event selected) | Action |
|---|---|
| Left / Right | nudge as before |
| Up / Down | deselect and put the cursor in the neighbouring lane at the event's start |
| Delete / Backspace | delete as before |
| Escape | deselect, cursor goes to the event's start |

Rules shared with the mouse path: uniform mode refuses with the same message; a locked lane refuses
with the same message; overlap and the 32-event limit show the same messages (the new event is clamped to
the phrase end; a start that falls inside an existing event of the same lane is refused). After every
keyboard action the canvas `setDescription()` says lane name, position (bar.beat) and what happened, and
`onMessage` is called, so a screen reader announces it.

## U2 Narrow-event move handles

Events narrower than about 12 px lost their Move area to the two 6 px resize edges, so they could only be
moved with the arrow keys, and a 3 px event was hard to hit.

- Hit test tolerance: 5 px (converted to ticks) around an event body.
- If the event is narrower than 16 px on screen, a plain drag always moves it. Shift-drag resizes the end,
  Alt-drag resizes the start. Wider events keep the edge behavior.
- Narrow events draw a small grip tab above the body so the handle is visible.
- The canvas tooltip documents the modifiers.

## U3 Phrase-fit badge (meter/phrase change)

Core: `core/control/PhraseFit` `checkPhraseFit(lane, phraseTicks, permissions)` counts events that start at
or after the end of their ring (phrase, or the operation's polyrhythm loop) and events that overhang it, and
how many of those are locked.

UI: while the lane is in explicit mode and `conflict()` is true the canvas draws a warning badge at the top
right: "N event(s) past the phrase end wrap around" (plus ", M locked" when some are locked). The tooltip
adds "Limit tab > Events x2 or /2 rescales them." The badge is recomputed on every paint, so it disappears
as soon as the events fit again. Nothing is moved or deleted automatically.

## U4 Scenes tab honesty

The Scenes tab said "Duplicated.", "Scene reset" and "Effect reset." even when the C03 locks kept fields
unchanged. Now each action compares the resulting scene with the intended result:

| Action | Success text | When locks kept something |
|---|---|---|
| Duplicate | "Duplicated." | "Duplicated, but locked fields of scene X were kept (Gen tab > Locks)." |
| Reset scene | "Scene reset (Undo available)." | "Scene reset, but locked fields were kept." |
| Reset effect | "Effect reset." | "Effect not fully reset: locked fields were kept." |
| Clear placement | "Placement cleared." | "Placement not cleared: it is locked." |

Failure texts are unchanged.

## U5 Undo granularity

Core: `core/scene/EditCoalescing` + `UndoStack::commitCoalesced(next, key, nowMs, 500)`.
`singleChangedNumericField(before, after)` identifies the one numeric field (operation depth/slice,
placement numbers, activity limits, phase offset) that differs; `coalesceKey(scene, before, after)` is that id
with the scene index, or 0. The processor's `setVariationSettings` and `setOperationSettings` pass the key
and `juce::Time::getMillisecondCounterHiRes()`; edits of the same field of the same scene whose gaps are at
most 500 ms merge into the first undo entry (the window slides). Toggles, event edits, locks, name changes,
Copy/Duplicate/Reset, undo, redo and any other field always start a new entry. A drag still commits once on
release; keyboard repeat or wheel steps after it within 500 ms join that entry. One Undo reverts the whole run.

## U6 Accessible names and descriptions

`finishControlMenus` (called once per panel after construction, and for every inspector tab) now also, for
every `juce::Slider`, `juce::ComboBox`, `juce::Button` and `juce::TextEditor` below the root:
- `setTitle(name)` when the title is empty. Name = the menu name already derived, else the first sentence of
  the tooltip, else the component name. Buttons with text keep their text as name.
- `setDescription(tooltip)` when the description is empty and a tooltip exists.
Tab buttons get their long tab names as description. Existing titles/descriptions (Visual views) are never
overwritten. JUCE exposes `Component::getTitle()`/`getDescription()` through its `AccessibilityHandler`.

## U7 Visual window description rate

The five Visual views rewrote their accessibility description on every 30 Hz refresh (string building plus
handler invalidation). `core/viz/TextThrottle` gates this: a new description is applied only if the text
changed AND at least 250 ms passed since the last applied one, so the rate is at most 4 Hz, and 0 Hz while
nothing changes. The newest text is applied by a later refresh, because the refresh keeps running. Painting
and the 30 Hz (10 Hz Reduce motion) refresh rate are unchanged.

## Acceptance tests

| ID | Check | Where |
|---|---|---|
| UI-PF-1..4 | phrase-fit counts: uniform/empty fit, past-end and overhang, exact fit, locked counts, polyrhythm ring | `PhraseFitTests` |
| UI-UC-1..6 | field detection, run coalescing, window/sliding/backwards clock, key and structural breaks, no-op commit, bounded history | `EditCoalescingTests` |
| UI-TH-1..3 | text throttle: first apply, unchanged text, interval, change after interval, reset, backwards clock | `TextThrottleTests` |
| UI-KB-1, UI-NH-1, UI-PF-5, UI-SC-1, UI-AX-1, UI-VR-1 | keyboard creation, narrow handle, badge draw, Scenes tab messages, accessible names, description rate | manual in a JUCE build (unverified here) |
| UI-UC-7 | processor: 20 slider steps within 500 ms are one undo step; a toggle between them splits | `PluginProcessorTests` (processor test binary; ran green locally 2026-10-20, see the verification note in W10-UI-ISSUES.md) |

## Verification note

See "Status" at the top. The local processor test run used `-O1` and JUCE objects built by another worker; CI
(JUCE 8.0.7, Release) remains the authority.
