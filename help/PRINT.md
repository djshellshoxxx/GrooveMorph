# Print to WAV

The **Print** tab (last tab of the inspector) records what GrooveMorph is outputting for a whole number of bars and saves it as a WAV file you can drag straight into your DAW.

## Make a print

1. Start the transport. In a DAW the host decides; in the Standalone use Play.
2. Open the **Print** tab, pick the number of bars (1 to 16) and press **Print**.
3. The status line says "Waiting for the next bar line". Recording starts on that bar line and stops after exactly the chosen number of bars.
4. The file appears at the top of the list.

Files go into `Documents/GrooveMorph Prints` and are named `GrooveMorph_<scene>_<bpm>bpm_<bars>bars_<timestamp>.wav`. They are 24-bit stereo at the session sample rate.

## Options

- **Wet only** prints just the effect signal, without the dry signal. It needs Intensity x Dry/Wet above 1%; if it is lower, that part of the print is the full output (the message under the button tells you). With the band split or Bypass active, wet only is approximate.
- **Include tail** records a few extra seconds after the last bar (the same length GrooveMorph reports to your DAW as its tail) so replayed or reversed audio can finish. The tail sits after the bar-aligned part.

## When a print is thrown away

A print is discarded, and the message says why, if the transport stops, jumps (seek or loop) or the tempo changes before the last bar has been recorded. A print is also refused when the bars would be longer than 90 seconds at the current tempo. Press the button again (it reads **Cancel** while a print is running) to cancel by hand.

## The list

The list keeps the last 20 prints; older files stay in the folder.

- **Drag a row** into a track in your DAW (or any folder) to use the file. The DAW gets a copy. If dragging does not work in your host, press **Reveal** and drag the file from your file manager.
- **Reveal** shows the file in the file manager.
- **Delete** moves the file to the trash (it is deleted for good only if your system has no trash).
- **Use as loop source** loads the print, without its tail, as the beat-locked loop and sets the input source to Captured / imported loop, so you can process your own print again. Loops are limited to 20 seconds. For files found in the folder from an earlier session the length is worked out from the bars and tempo in the file name.

## Limits and notes

- A print runs in real time; there is no faster-than-real-time render.
- Memory for a print is only allocated while it runs (up to about 38 MB at 48 kHz for the longest print) and is released afterwards.
- A finished print is saved by the Print tab. If you close the plugin window right after the last bar, the file is written when you open the window again.
- Dragging into a DAW has not been tested in any host yet.
