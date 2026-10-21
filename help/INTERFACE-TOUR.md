# The new interface (beta.2)

Beta.2 replaces the fixed-size window with a resizable single-window layout. Nothing was removed; controls moved. This page is written from the redesign spec (`docs/specs/UI-REDESIGN.md`). Nobody has used it in a real window or DAW yet (TODO B-13), so if something differs, trust the screen and tell us.

## Window size and UI scale

- Opens at 1180x760. Drag a corner to resize between 960x640 and 2200x1400; everything scales with the window.
- Settings (gear, top right), UI scale: 75, 100, 125, 150 or 200 percent. It is stored with the project and does not depend on your host's DPI setting.

## Layout tour, top to bottom

1. **Header.** GrooveMorph name, the preset bar (previous, name with an unsaved-changes dot, next, Save menu), then Play, Stop, Easy, Visual, Bypass and the Settings gear.
2. **Performance band.** Four big scene pads A to D (they say in words which one is sounding or queued), the Transition menu, Trigger and Cancel. On the right: knobs for Intensity, Dry/Wet, Morph (with its target menu) and the large Chaos knob, plus Surprise me, Undo and Redo.
3. **Timing strip.** A thin bar showing where you are in the phrase, the timing text, status text, and the **Timing...** button.
4. **Effects rack.** Which scene you are editing, its name, Copy FX, and the Audition group (Preview, Hear stored/edited, Apply, Revert). Below that, seven effect cards. Each has a colour bar, an icon, the name, an Enable toggle, Depth and Slice. The effect colour is the same in the rack, the lanes, Euclid and the Visual window.
5. **Workspace.** Tabs: Pattern, Generate, Scenes, Morph, Limits, Input, Perform, Info, Print. Pattern is the event timeline, full width, with the Euclid / chance drawer on the right (it collapses on short windows).
6. **Footer.** A hint line for whatever control the mouse is over, the MIDI note field and status, and CC link status.

## Where the old controls went

| Old place | Now |
|---|---|
| Manual tempo, Manual beats per bar, Beat unit, Timing method, Phrase length (main column) | Timing popover, from the Timing... button on the timing strip |
| Transport and Easy / Visual buttons | Header |
| Demo loop, Reset to defaults | Settings gear menu |
| Scene A to D buttons, Transition, Trigger, Cancel | Performance band |
| Intensity, Dry/Wet, Morph, Chaos, Surprise me, Undo, Redo | Performance band |
| Seven effect rows (Enable, Depth, Slice) | Effects rack cards |
| Edit scene, scene name, Copy FX, Preview / Hear / Apply / Revert | Effects rack header |
| MIDI note and return-on-note-off | Footer |
| Inspector column (Place, Gen, Scene, Morph, Limit, Input, Perform, Info, Print) | Workspace tabs, now using the full width. "Place" is now called Pattern, "Gen" is Generate, "Scene" is Scenes, "Limit" is Limits |

## Shortcuts

| Keys | Action |
|---|---|
| Space | Play / Stop (where the host allows) |
| 1 to 4 | Trigger scene A to D |
| T | Trigger |
| Esc | Cancel a queued change |
| Ctrl+Z (Cmd+Z on macOS) | Undo a scene edit |
| Ctrl+Shift+Z or Ctrl+Y (Cmd+Shift+Z) | Redo |
| F1 | Open the help popover |
| Tab | Move focus in visual order |

The editor takes keyboard focus when you click it, so the shortcuts work without first clicking a control. Lane editing keys are unchanged (see [Keyboard editing](KEYBOARD-AND-ACCESSIBILITY.md)).

## Knobs

Drag vertically (about 200 px for the full range), hold Shift for fine steps, double-click to reset, use the mouse wheel while hovering, right-click for the value and MIDI learn menu. A value pops up while you drag.

## Timing popover

Click Timing... on the strip. It holds Timing method (Automatic, Host PPQ, Sample Clock), Manual tempo, Manual beats per bar, Beat unit and Phrase length. These are the same parameters as before, so existing projects and automation are unaffected. See [Timing and transitions](TIMING.md).

## Easy mode

Easy keeps its separate full-window view and first-run guide, restyled to match. Switch with the Easy button in the header. See [Easy mode](EASY.md).
