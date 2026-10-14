# User test guide (Windows x64 Standalone alpha)

**Unsupported or incomplete (read first):** per-scene transition smoothing time, time stretching, loop-audio persistence (gesture curve,
evolution settings and MIDI links are saved), Linkwitz-Riley / steeper crossovers, loop-aware Reduce, CLAP. See ALPHA-FEATURE-STATUS.md (the Info tab in the app lists them too).

## Launch and audio setup
1. Unzip; run `GrooveMorph.exe` (no installer). Windows SmartScreen may warn: the build is unsigned.
2. In the Standalone window use **Options > Audio/MIDI Settings**: choose your output device. Pick an input only if you want live input (beware feedback; mute the monitor or use headphones).
3. Tick **Demo loop** (top bar) to replace the input with a built-in drum loop. In Standalone, *Timing method: Automatic* behaves as a free-running clock at the Manual tempo (default 120 BPM, 4/4, 4-bar phrase).
4. JUCE Standalone shows a yellow banner that audio input is muted to avoid a feedback loop. That is expected; the Demo loop replaces the input anyway. The window is 1290x740 by default (minimum 1230x700) and scales itself down to fit smaller displays.
5. Raise **Intensity** (default 0.5; 1.0 = full effect, 0 = dry). Keep the volume low at first.

## Try the main effects
- Scene **A** repeats, **B** gates, **C** reverses + half-time, **D** tape-stops. Click a scene button to queue it using **Transition** (Immediate/Next Beat/Next Bar/Next Phrase/Manual). Green = sounding, amber = queued, "[editing]" = the scene the left panel edits.
- Toggle effects and move Depth/Slice for the edited scene. Use **Preview** to audition edits without committing, **Hear stored** to compare, **Apply preview** / **Revert preview**.

## New controls (inspector, right)
- **Placement:** tick *Enable phrase placement*, choose *Last bar* (effects only in the final bar). Try *Custom window* with Start/Span, *Every N phrases*, *Protect downbeat*. Tick *Use event lanes* (or **Convert to events**) and drag in a lane to create events; drag edges to resize; Delete removes.
- **Generator:** set a Seed, Style and Distance, press **Generate preview**, then **Apply preview** or **Revert preview**. **Regenerate >** gives the next candidate; the same seed/candidate always reproduces. Use Locks to protect fields.
- **Scenes:** duplicate A to B, reset a scene or effect; Undo reverts each.
- **Morph/Return:** morph toward another scene with Amount; set *Return after (bars)* then switch scenes and watch it return on the bar line.
- **Limits:** limit effects at once and set priorities; set spacing/coverage and press **Analyze**, then **Reduce events**.
- **Place (events):** select an event to set its own slice, priority and *Restart effect cycle*; set a lane's loop length (e.g. Gate = 3 beats vs Repeat = 4) for polyrhythm.
- **Input:** pick *Demo drum loop*; press **Capture** (starts on the next bar line) or **Import file...**, then choose *Captured / imported loop*. Try *Band split* (Protect lows / Lows only), *Audio trigger* (Next scene) and the MIDI macros.
- **Perform:** launch scenes, move the macros, **Record** a gesture while moving Morph amount and **Play** it back, and enable **Evolve scene**.

## Visual evaluation checklist (record defects)
1. Window opens, nothing clipped at the default size; resize to the minimum (1230x700) and to full screen.
2. Top bar: Demo loop and Bypass visible; status line shows "Playing | editing X | bar | beat" and the playhead moves.
3. Scene buttons: active/queued/editing states are legible and update when switching.
4. Each inspector tab: labels fully visible, tooltips appear on hover, disabled controls are greyed.
5. Event lanes: grid, bar numbers, snap lines, shaded placement window, playhead; events draw with correct colours and selection outline.
6. Create/move/resize/delete events with the mouse and arrow keys; try overlapping events in one lane (should be refused with a message).
7. Generate -> Preview indicators: status shows "Preview"; Apply/Revert enable correctly; Undo/Redo behave.
8. Change phrase bars / beats per bar and check the lanes rescale.
9. Close and reopen the window; confirm no crash.
10. Note anything that sounds wrong (clicks, silence, wrong timing). Expect rough edges: this is untested by ear.

## Newest features to try (merged alpha)
- **Scene start phase (Perform tab):** give scene B "Restart at launch", launch it on a bar and watch "Pattern is shifted by ..." change; "Restart scene now" restarts the active scene; stopping and playing, or seeking, returns to the scene's own phase.
- **Band slope (Input tab):** switch the crossover between 6 and 12 dB/oct with "Protect lows"; with Intensity at 0 the sound must stay unchanged.
- **Protected regions (Limit tab):** add a region, move its start/length, and check no effect sounds inside it; the lane view shows it as a red band.
- **Lock event (Place tab):** lock one event; it can no longer be moved or deleted, and generation/Reduce keep it.
- **Events x2 / /2 (Limit tab):** after changing the phrase length, rescale events; locked events keep their place.
- **Saving:** set a gesture curve, evolution and a MIDI link, save the project, reload, and confirm they come back (loop audio does not).
- **Right-click** any control: exact value, copy/paste, reset, MIDI learn; **Visual** opens the live picture window.

