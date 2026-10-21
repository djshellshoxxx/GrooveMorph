# MIDI notes, MIDI learn and host automation of effects

## Scene notes can no longer be doubled by accident

Each scene (A to D) has one MIDI note that switches to it (lower left of the main window, "MIDI note").
If you pick a note that another scene already uses, GrooveMorph asks first:

- **Swap**: the scene you are editing takes the note, and the other scene gets the note you just gave up.
  Nobody loses a note and no two scenes share one.
- **Cancel**: nothing changes and the slider goes back.

A swap is one step in **Undo**, so one click on Undo puts both notes back. Dragging the slider asks when you let go.
Using the keyboard or mouse wheel on the slider waits for a short pause, so stepping past a used note does not
ask a question for every step.

Older projects that already have two scenes on the same note still load and play exactly as before (the first
scene wins). The MIDI line in the main window tells you about it; set the note of one of the two scenes again and
you will be offered the swap.

**All Notes Off / panic.** If "Return on MIDI note-off" is on and your keyboard or host sends All Notes Off
(CC 123) or All Sound Off (CC 120) instead of a note-off, GrooveMorph now returns to the previous scene, just as a
real note-off would. Before this, the held note stayed "held" and the return never came.

## MIDI learn

Right-click a control, choose **Link to MIDI controller (learn)**, then move the knob or fader you want.

- If several controllers move at the same moment, the one that **moved the most** is used (not the one with the
  lowest number).
- A quick pulse, for example a pad that goes to 127 and back to 0, is now noticed even when it is over between two
  checks.
- The table holds **64 links**. When it is full, the menu item says so and is greyed out. If learn cannot link
  (for example the table filled up), a message appears and the status line shows "MIDI link table full". Unlink a control
  (right-click it, **Unlink MIDI**) and try again. Re-learning a control that is already linked always works.

Applying a linked controller still happens about 20 times a second; see "Rates" below.

## Automating each effect from the DAW (live trims)

Every effect now has three host parameters that act as live trims on the **scene that is sounding**:

| Parameter (host name) | Range | Default | What it does |
|---|---|---|---|
| `opNEnable` ("Repeat / stutter: Enable trim") | Scene, Off, On | Scene | Scene uses the scene's own on/off. Off silences the effect. On forces it on. |
| `opNDepth` ("... Depth trim") | 0 to 1 | 1 | Multiplies the scene's depth. 1 changes nothing. |
| `opNSlice` ("... Slice trim") | 0.25 to 4 | 1 | Multiplies the scene's slice length (result stays between 1/16 and 4 beats). 1 changes nothing. |

`N` counts from 0 in the effect order: 0 Repeat, 1 Reverse, 2 Gate, 3 Half-time, 4 Tape stop, and further effects
take the next numbers (for example `op5Depth`, `op6Slice`). These ids never change, so automation you record now keeps
working when effects are added later.

Trims are an overlay: they never edit your scenes, add nothing to Undo, are not saved inside scenes, and ignore
the scene locks (locks protect the editor, not live moves). With every trim at its default the sound is identical to
before. You can MIDI-learn them like any host parameter. The Visual window still draws the scene as stored, not
the trimmed version.

## Rates (how often things are checked)

| What | How often |
|---|---|
| MIDI notes, program changes, CC 120/123 | at their exact position in the audio block |
| Linked controllers, MIDI macros, evolution steps, gestures, loop capture | about 20 times a second (up to 50 ms late) |
| Main window refresh | 10 times a second |
| Visual window | 30 times a second (10 in Reduce motion) |
| Screen reader descriptions of the Visual window | at most 4 times a second, and only when the text changed |

Not tested in a DAW or with hardware.
