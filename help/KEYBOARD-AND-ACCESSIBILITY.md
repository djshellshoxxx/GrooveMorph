# Keyboard editing, event lanes and screen readers

## Creating and editing events with the keyboard (Place tab)

Click the event-lane canvas once (or tab to it) so it has keyboard focus. With **no event selected** a small white
cursor shows where the next event will go.

| Key | What it does |
|---|---|
| Left / Right | Move the cursor by one snap step (Shift: one beat) |
| Up / Down | Move the cursor to the lane above or below |
| Enter, Insert or N | Create an event of one snap step at the cursor and select it |

With an event selected the keys work as before (Left/Right nudge, Delete removes). New: **Escape** deselects and puts
the cursor at the event's start, and **Up / Down** deselect and move the cursor to the next lane. The same rules as
the mouse apply: nothing is created in uniform mode (press Convert to events first), in a locked lane, over another
event of the same effect, or beyond 32 events, and a message says why. A screen reader announces the lane and the
position (bar and beat) of the cursor and of a new event.

## Very narrow events

Events thinner than about 16 pixels used to be hard to grab. Now a plain drag **moves** them, **Shift-drag**
resizes the end and **Alt-drag** resizes the start. A small tab above a thin event marks the place to grab, and
clicking within a few pixels of it counts as a hit.

## Scene shortcuts (editor window)

When the editor window has focus and you are not typing in a text box: **1, 2, 3, 4** request scene A to D with the
current Transition mode, **T** fires a queued switch (Manual transition mode), **Esc** cancels the queued switch, and
**Space** starts and stops the internal transport (Standalone). Shortcuts with Ctrl, Alt, Shift or Cmd are left alone.
These keys are new in the beta build and have not been tried on a real desktop yet.

## "N events past the phrase end"

If you shorten the phrase or change the meter, events keep their positions, so some may now lie beyond the end and
wrap around to the start. An orange badge at the top right of the canvas counts them (and how many are locked).
Nothing is moved for you. Use **Limit tab, Events x2 or /2** to rescale, or move the events by hand. The badge goes away
when everything fits again.

## Scene tab messages tell the truth

If a lock kept some fields unchanged, Duplicate, Reset scene, Reset effect and Clear placement now say so
("..., but locked fields were kept") instead of reporting full success.

## Undo steps for slider steps

Holding an arrow key or turning the mouse wheel on a scene slider (depth, slice, placement and limit numbers, phase
offset) used to add one Undo entry per step. Steps on the same slider within half a second of each other now share one
entry, so one Undo takes the slider back to where it was before you started. Toggles, event edits, locks and a pause of
more than half a second always start a new entry.

## Screen readers

Every slider, drop-down, button and text field now has an accessible name and, where it has a tooltip, a description.
The Visual window refreshes its spoken descriptions at most four times a second, and not at all while nothing
changes. Not tested with a screen reader on Windows or macOS.
