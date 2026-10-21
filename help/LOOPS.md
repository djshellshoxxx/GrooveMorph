# Loops: stretch, saving and the waveform

All of this is on the **Input** tab. Nothing here has been checked by listening in a DAW yet.

## Stretch to host tempo

- **Off (default):** the loop is locked to the beat grid and simply plays faster or slower with the tempo, so its pitch moves with the tempo. This is the original behaviour and is unchanged.
- **On:** the plugin renders a pitch-preserving copy of the loop in the background. When the tempo has been steady for about a fifth of a second it works out the stretch (loop tempo divided by host tempo) and swaps the new copy in. The audio never waits for it.
- The ratio is limited to 0.5 to 2.0 and to the 20 second buffer. Outside that the rest is plain speed change, and the line under the checkbox says so.
- Until the new copy is ready after a tempo change, the loop plays at the speed of the old tempo. Tempo automation re-renders after each pause.
- Known artifacts of this kind of stretching: a drum hit can be doubled or smeared a little, dense chords can sound slightly phasey, and very low notes (below about 50 Hz) are not followed well. Try it on your material and leave it off when you prefer the varispeed sound.
- The saved and displayed loop is always the original, never the stretched copy.

## Saving the loop with the project

- The loop (captured or imported) is now stored inside the plugin state, so it comes back when you reopen the project. The Input source is restored to **Captured / imported loop** if it was selected.
- Audio is stored as 16-bit audio, compressed without further loss. That is not bit-for-bit identical to a 32-bit float capture. Peaks above full scale are clipped when saved.
- A loop longer than 20 seconds cannot be loaded at all. If a loop is too big to save (more than about 10 MB after compression, only possible with extreme sample rates) the project is saved without it and the Input tab shows a warning.
- Projects saved by older versions have no loop and load exactly as before; a loop already loaded in the session is kept.
- If a project's saved loop is damaged it is ignored, the rest of the project loads, and the Input tab says the loop could not be restored.

## Capture safety

- A capture stops with a message if the tempo changes while recording (more than 0.1 %) or the meter changes, because the recorded length would no longer match its beats.
- A finished capture is never thrown away. If it cannot be loaded straight away it is kept and retried.
- The demo drum loop follows the meter: the snare sits on the even-numbered beats (2 and 4 in 4/4, beat 2 in 3/4).
- A loop keeps its length in beats if you change the meter later.

## Waveform

The strip under the loop line draws the loop: thin lines are beats, brighter lines are bars (using the current meter), and the white cursor is the playhead while the loop source plays.
