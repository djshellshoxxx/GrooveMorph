# Right-click menu, MIDI links, Play/Stop and Reset

## Play, Stop and Reset (top row)

- **Play** starts the internal transport from the beginning. **Stop** halts it and returns to the beginning; effects, the demo
  loop and loaded loops stop with it (a stopped loop is silent). **Space** toggles Play and Stop.
- These work in the Standalone app, and when *Timing method* is **Sample Clock** and the host reports no play state.
  When a host decides play and stop, the buttons are greyed and only show the host's state; use the host's transport.
- **Reset** asks first, then puts everything back to the factory defaults: all controls, the four scenes (effects, names,
  placement, MIDI notes), MIDI links, loaded loops, the input source, evolution and gestures, and the transport.

## Tempo and loops

- Changing the tempo while playing no longer makes the position jump; the beat carries on from where it was.
- A loop plays at its **original speed when the plugin tempo equals the loop's own tempo**; at other tempos it plays
  proportionally faster or slower (and its pitch follows, there is no time-stretching).
- **Import file**: type the loop's tempo in the **BPM** box if you know it (best, and it keeps any tail or pre-roll in the
  file). Otherwise leave it empty and **Auto bars** picks 1, 2, 4, 8 or 16 bars so that the tempo is nearest your current
  tempo. The message and the loop line show what was decided ("16 beats = 140.0 BPM"). **Set tempo to loop** sets the
  manual tempo to the loop's tempo.
- Files are resampled to the session's sample rate, and a loaded loop now survives the host re-preparing the plugin.

## Right-click menu on every control

Right-click any slider, drop-down or checkbox:

| Item | What it does |
|---|---|
| Enter exact value... | Type a number (sliders). Out-of-range values are clamped. |
| Copy value / Paste value | Copies the number (or the choice, or on/off) to the clipboard and pastes it into any other control. Paste understands numbers in text such as "128.5 bpm". |
| Reset to default | Back to the control's default. |
| Link to MIDI controller (learn) | Then move a knob or fader on your MIDI device: the first controller that moves is linked. The control shows an orange outline while waiting and a small "CC n" tag once linked. |
| Re-learn / Unlink MIDI | Change or remove the link. |

What can be linked: every host parameter (intensity, dry/wet, tempo, meter, phrase bars, timing and transition modes,
bypass, morph, auto-return, band split, audio trigger) and, in the main window, each effect's **on/off, depth and slice**
for the *sounding* scene. Controller moves on scene settings are live performance moves: they add no Undo entries, are ignored while a
preview is running, and respect locks. Links are saved with the project and cleared by Reset. One controller can drive several controls.

## Tooltips

Hover any control, effect, tab or Visual-window panel for a plain-language explanation. An automated test fails if any control in the main window (including every inspector tab) lacks one; the Visual window has its own tooltips, which that test does not cover.

## Limits

MIDI links use controller (CC) messages only, are applied about 20 times a second (so very fast sweeps are stepped),
and scene links act on the scene that is sounding, not the scene being edited. Not validated in a DAW or with hardware.
