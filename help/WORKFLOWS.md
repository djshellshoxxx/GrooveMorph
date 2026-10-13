# Practical workflows

## Build four contrasting variations

1. Put GrooveMorph on a playing drum or percussion track as an audio effect.
2. Set Dry / wet and Intensity above zero, keep Bypass unchecked, and choose Automatic timing.
3. Use **Edit A** in the editor selector; rename the variation if useful.
4. Enable Repeat/stutter with a moderate depth and short slice. Toggle Reverse/Gate/Half-time/Tape stop one at a time to hear their contribution. Each variation keeps independent operation settings.
5. Repeat for Edit B, C and D, making clear contrasts (for example, a gated pattern, a reverse-slice accent, and a more pronounced tape-stop treatment). These are suggested settings, not factory presets.
6. Choose Next Bar, start the host, click A/B/C/D in the playback row and observe active/queued labels.
7. Save your project and reopen it to test state restoration.

## Audition a variation without committing

1. Choose a target with the **Edit A–D** selector.
2. Press **Preview**; operations edited during the audition update the preview.
3. Press **Hear stored** to compare with the stored variation. The button reads **Hear edited** while comparing.
4. Press **Apply preview** to commit or **Revert preview** to discard. You cannot change the edit target during preview. Closing the window discards unfinished audition edits.
5. After applying, use Undo or Redo if needed (when enabled).

Editing operation settings **without first pressing Preview** writes them directly to the variation bank and can be undone through the scene edit history. Preview is an explicit alternative workflow, not a prerequisite for editing.

## Control variation changes from MIDI

1. Ensure the DAW routes note events **to GrooveMorph's MIDI input** (not only to the instrument generating audio).
2. Choose Edit A and use the MIDI note slider to assign a note number in 0–127. Repeat for B/C/D using distinct note values.
3. Enable **Return on MIDI note-off** only when you want releasing a note to return to the previous variation according to the processor's note-off policy.
4. Play the mapped MIDI notes, watch the event counter and active/queued state. The selected transition policy still matters to scheduler behavior.
5. If the MIDI counter stays at 0, fix routing or host plugin format/track configuration. It does not by itself diagnose hardware or plugin failure.

MIDI note mapping is disabled while a preview audition is active, so finish the preview before changing assignments.

## Standalone preview without a host playhead

1. Start the Standalone app and configure an audio device/input in the standalone wrapper as supported by JUCE.
2. Select **Sample Clock** and valid manual tempo/meter/phrase settings.
3. Feed audio to the effect and set Intensity/Wet above zero.
4. If an external host explicitly tells the processor it is stopped, Sample Clock does **not** override that state.
5. Start with Immediate scene switching to distinguish input/output routing issues from musical-grid waiting.

## Live performance safeguards

Set and test the phrase length and transition policy before playing. Prefer bar/phrase changes for structured section changes and Manual when you need explicit launch control. Test tempo changes, seek/loop behavior, MIDI note releases and reloading presets in the exact host and buffer configuration before a public performance. This MVP has not yet completed independent host/device acceptance testing.

## Current workflow limitations (2026-10-13)

Use distinct MIDI note assignments. Duplicate notes are accepted, and the first
matching variation wins without a warning. Scene buttons/MIDI can change the
active variation while the host's Variation parameter still shows its earlier
value; inspect the plugin active/queued labels and verify automation behavior.
These are known gaps, tracked in [TODO](../docs/TODO.md).

Undo/Redo covers scene names, operation edits, mappings and scene-bank changes;
global tempo/intensity/dry-wet/transition parameters are outside that scene history.
Per-operation controls cannot currently be automated as separate host parameters.
A slice's effective duration may be shorter than requested at slow tempos because
history is bounded; inspect its tooltip for operation-specific limits.
