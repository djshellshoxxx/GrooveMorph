# A07 Accessibility, keyboard, scaling and screen reader

Status: PARTIALLY IMPLEMENTED. Lane keyboard authoring, accessible names, thin-event handles, phrase-end badge, scene shortcuts 1-4/T/Esc; UI scale, F1, capped-slice indicator and screen-reader pass are not built (TODO B-04, B-01). Updated 2026-10-20 by the A1 completeness audit.
Source item: `docs/TODO.md` A07 ("help entrypoint, effective slice-limit feedback, keyboard/scaling QA"). Related: [F04](F04-PERFORMANCE-MODE.md), `docs/visual-feedback/SPEC.md`.


## Beta audit status (2026-10-20)

- Built (W10, compiled in CI only): section 3.2 subset in `LaneCanvas` (Left/Right/Up/Down/Enter/Insert/N/Delete/Esc) and accessible names (UI-AX-1). Built by the audit: scene shortcuts 1-4, T, Esc (section 3.1).
- Not built: Ctrl/Cmd+Z in the editor (the Undo button exists), F1 help, Tab to next event, Alt+Left/Right resize, L to lock, the UI scale setting (3.4), the capped-slice indicator (3.6), reduce motion outside the Visual window.
- Nothing here has been used with a screen reader or on a real desktop (B-01).
- Acceptance rows below carry audited statuses; the full join of ID, test, implementation and UI wiring is in [COMPLETENESS-MATRIX.md](COMPLETENESS-MATRIX.md).

## 1. Objective

Every control and every drawn view can be operated with the keyboard alone, announced by a screen reader, and used at 100% to 200% UI scale. Limitations are visible, not hidden.

## 2. Current behavior (facts)

Keyboard
- `GrooveMorphAudioProcessorEditor::keyPressed`: Space toggles the internal transport, only when `isInternalTransportActive()` (Standalone or no host play state).
- Event canvas (`InspectorPanel.cpp`, `setWantsKeyboardFocus(true)`): Delete/Backspace deletes the selected event, Left/Right nudges by the snap unit, Shift+Left/Right by one beat; locked events do not move.
  There is NO keyboard way to create an event, select the next/previous event or resize.
- Visual window: `keyPressed` handles Esc and C (`VisualWindow.cpp`), `setWantsKeyboardFocus(true)`.
- `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` in `CMakeLists.txt`.

Screen reader / names
- JUCE 8 provides default accessibility handlers for standard widgets. `finishControlMenus` gives every `Menu*` control a name taken from its tooltip or text when it has none.
- The Visual window views set `setTitle`/`setDescription` (Warp Weave, Ledger, Rings, Stations, Orbit); a test asserts every view has an accessible name (PluginProcessorTests "every view has an accessible name"). The Ledger copies as text.
- The event canvas, scene buttons and tab bar have no explicit accessible title/description/value text beyond JUCE defaults. Not verified with NVDA, JAWS, Narrator or VoiceOver (open item in `docs/TODO.md`).

Scaling and layout
- `PluginEditor.cpp`: initial size 1290x740; `setResizable(true, true)`, limits 1230x700 .. 1900x1100; on displays smaller than that the whole editor is shrunk with `setScaleFactor(max(0.5, fit))`.
- No user UI-scale setting; no minimum font size guarantee at 50% scale.

Motion and colour
- Visual window has "Reduce motion", palette checks (`tests/core/PaletteTests.cpp`); human colour-blind review is pending.

## 3. Required behavior

### 3.1 Keyboard map (editor, when the editor window has focus; shortcuts never fire while a text field is being edited)
| Key | Action |
|---|---|
| 1, 2, 3, 4 | Request scene A..D with the current transition mode |
| Esc | Cancel a queued switch; closes an open popup first |
| T | Trigger the queued switch (Manual mode) |
| Space | Toggle internal transport (unchanged) |
| Tab / Shift+Tab | Move focus through controls in visual order (explicit focus order set per tab) |
| Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z | Scene undo / redo (when not auditioning) |
| F1 | Open the help entry point (opens `help/README.md` location or an in-app help panel) |
Shortcuts are listed in the Info tab and in `help/CONTROLS.md`. Keys the host needs (transport, save) are not captured when the host owns them: only handle keys the focused component did not use, and never consume Space when the internal transport is inactive.

### 3.2 Event canvas keyboard authoring (owner W10)
With the canvas focused: Up/Down moves the lane cursor between the five operation lanes; Left/Right moves the time cursor by the snap unit (Shift = one beat, Ctrl/Cmd = one bar);
Enter creates an event of the snap length at the cursor (same validation as mouse creation: overlap, capacity, locks, protected regions give an announced failure); Tab/Shift+Tab selects next/previous event;
Delete removes; Alt+Left/Right resizes the end; L toggles the event lock; announcements go through the accessibility handler's value/title text ("Gate event 2 of 4, beat 3.00, length 0.50 beats, depth 80 percent, locked").

### 3.3 Accessible names
Every interactive component has a non-empty `setTitle` (name), a `setDescription` or `setHelpText` (tooltip text) and, for sliders/combos/toggles, the standard value text including units ("Crossover 120 Hz"). Scene buttons announce
"Scene B, sounding" / "queued" / "editing". The event canvas and Visual views expose a text summary equal to what the Ledger prints. Status labels that change (queued switch, capture state, conflict notices) use polite announcements.

### 3.4 Scaling
A "UI scale" setting offering 75%, 100%, 125%, 150%, 200% stored per user (user application-data file next to the generator history, not in the project), applied with `setScaleFactor`. The existing shrink-to-fit logic remains as the upper bound.
Minimum legible font height: 11 px at the chosen scale; no text is clipped at any supported scale and minimum editor size (check visually and with the text-bounds helper). Layout must reflow when resized between the limits.

### 3.5 Motion, colour, contrast
Reduce motion is honored by every animated element (not just the Visual window). Text/background contrast at least 4.5:1 for normal text and 3:1 for large text and graphical objects, computed in `PaletteTests`. State is never conveyed by colour alone.

### 3.6 Visible limitations
Effective slice limits (bounded 4 s history: repeat under 1.33 s, reverse under 2 s, half-time/tape stop under 4 s; the gate uses the full value) are shown in the control tooltip today (`PluginEditor.cpp`); target: also show a small "capped" indicator next to the slice value when the requested slice exceeds the effective limit.

## 4. Parameters and state

No host parameters. UI scale is a per-user setting file, not project state. No scene keys.

## 5. Real-time and threading

UI-only. No audio-thread changes. Announcements and cursor updates run on the message thread.

## 6. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| A07-T01 | Visual views have accessible names and descriptions; Esc handled; unrelated keys not swallowed | EXISTS | `tests/PluginProcessorTests.cpp` "every view has an accessible name", "escape is handled", "unrelated keys are not swallowed" |
| A07-T02 | Palette distinctness / contrast checks | EXISTS | `PaletteTests.cpp` "ink on panel >= 4.5"; `PaletteTests.cpp` "dim text on slate >= 4.5"; `PaletteTests.cpp` "warning text on slate >= 4.5" |
| A07-T03 | Editor shortcut keys 1-4, T, Esc, undo/redo, F1 behave per 3.1 and are ignored in text fields | MANUAL | `PluginEditor.cpp` "A07 / F04 section 3.1" Keys 1-4, T and Esc are now implemented in PluginEditor::keyPressed (A1 audit); no editor-level test exists and nothing was typed in a real window (B-01). |
| A07-T04 | Keyboard-only event creation, selection, resize, lock on the canvas with validation messages | MANUAL | `InspectorPanel.cpp` "Keyboard cursor" Lane keyboard (Left/Right/Up/Down/Enter/Insert/N/Delete/Esc) is implemented in LaneCanvas; interaction is unverified without a display (W10-UI-ISSUES UI-KB-1, B-01). |
| A07-T05 | Every component under the editor and inspector has a non-empty accessible title; no two sibling controls share an identical title | EXISTS | `PluginProcessorTests.cpp` "UI-AX-1 every control has an accessible name"; `PluginProcessorTests.cpp` "every control has a tooltip" Names are asserted by constructing the editor in the processor test binary (CI only). Sibling-title uniqueness is not asserted. |
| A07-T06 | Focus traversal visits all controls in visual order without traps | MANUAL | Focus order is JUCE's default; no traversal test and no keyboard-only pass on a real desktop (B-01). |
| A07-T07 | UI scale setting persists across editor reopen; editor is usable (no clipped text) at 75/100/125/150/200% | DEFERRED | No user UI-scale setting exists (only the automatic fit scale in PluginEditor.cpp). Open as TODO B-04. |
| A07-T08 | Reduce motion honored by all animated elements | MANUAL | `PluginProcessorTests.cpp` "reduce motion can be turned on" Reduce motion exists for the Visual window only; nothing else animates. Preference tested at the processor level. |
| A07-T09 | Screen reader pass (NVDA/Narrator on Windows, VoiceOver on macOS) of the core workflow | MANUAL | Screen reader pass (NVDA/Narrator/VoiceOver) has not been done (B-01). |
| A07-T10 | Capped-slice indicator appears when the requested slice exceeds the effective limit | DEFERRED | Capped-slice indicator is not built; the limit is only in tooltips. Open as TODO B-04. |

## 7. Non-goals

Full WCAG certification, voice control, a separate high-contrast theme (reviewed after the contrast pass), localisation.
