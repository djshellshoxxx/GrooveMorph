# Euclidean patterns and chance

Two Place tab tools for making an effect lane rhythmic without drawing every event by hand.

## Euclidean patterns

Open the **Place** tab, switch on **Use event lanes**, then press **Euclid / chance** (next to the Snap grid).
The panel replaces the event controls under the lanes. The lanes stay visible above it so you can see what you place.

1. Pick the effect lane (top left) and the step size (top middle). One step is one division of a meter beat, so
   "2 per beat" gives eighth notes in 4/4.
2. Set **Steps** (pattern length, 1 to 32) and **Pulses** (hits, 0 up to Steps). The hits are spread as evenly as
   possible. 3 pulses in 8 steps is the classic tresillo, 5 in 8 is the cinquillo, 7 in 16 is a common samba feel.
3. **Rotation** slides the whole pattern later (positive) or earlier (negative) by whole steps.
4. **Length** sets how long each event lasts compared with one step.
5. Optional: **Accent every** makes every Nth hit an accent (full depth, always plays). The other hits are softer
   and use **Density** as their chance to play. With accents off, Density applies to every hit.
6. The ring on the right shows the pattern. Step 1 is at the top and has a double outline.

The pattern repeats across the whole phrase (or across the lane's loop length if you gave that lane one).

Buttons:

- **Preview** lets you hear the pattern without saving it. Use the main window's **Apply preview** to keep it or
  **Revert preview** to drop it.
- **Apply to lane** replaces the unlocked events of that lane with the pattern. It is one undo step.
- **Clear lane** removes the unlocked events of that lane. Also one undo step.

What it respects: a lane whose **Events** lock is on is refused. Individually locked events stay, and pulses that
would overlap them are skipped. Pulses inside a protected region are skipped, and nothing is added once the scene is
full (the status line tells you how many were dropped and why). If the scene was still in "uniform" mode, your other
enabled effects are turned into whole-phrase events first so they keep sounding.

## Chance

Every event can have a **Chance** from 0 to 100 percent, and every effect lane has a **Lane chance** that multiplies
it. An event with 50 percent in a lane at 50 percent plays in about 25 percent of the phrases.

- Select an event on the lanes, then drag **Event chance** in the Euclid / chance panel.
- Choose a lane at the top of the panel and drag **Lane chance** to change the whole lane.
- Events that do not always play are drawn hatched with their effective percentage.

The dice are repeatable. Each scene has its own hidden seed, and whether an event plays depends only on that seed,
the lane, the event and which repeat of the lane it is. The same song position therefore always gives the same
result: bouncing twice, changing the host block size or looping back all play the same thing. Moving an event
re-rolls only that event. Restarting the scene (Restart scene now, or a scene switch with a restart policy) replays
the sequence from the beginning.

Chance only applies to event lanes, not to the uniform whole-phrase mode. A locked event keeps its chance. A lane
whose Events lock is on also keeps its Lane chance. The Visual window's planned path uses the same rule, so it shows
what will really happen.

Scene files store chance as optional extra lines; older files and older builds ignore them, and scenes that do not use
chance are saved exactly as before.
