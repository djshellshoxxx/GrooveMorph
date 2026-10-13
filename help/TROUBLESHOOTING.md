# Troubleshooting and expected limitations

## I hear no audio

Check whether the source track generates audio before inserting GrooveMorph. GrooveMorph is an audio effect, not a synth. Confirm DAW routing, input/output channel assignment, interface device and track monitoring. If Bypass is active, disabling it should restore processing. Use Immediate transition, try a variation with an enabled operation and nonzero Depth, and raise Intensity and Dry / wet. Allow time for captured history to fill; Reverse and repeating operations need previous input samples.

## I hear only the original signal

Intensity of 0 is dry; Dry/wet of 0 is dry; disabled operations also yield little/no audible effect. In Host PPQ mode missing PPQ data can hold quantized requests or make rhythmic processing dry. Switch to Automatic or Sample Clock when appropriate and read the Timing label.

## Clicking a variation does not change it

Read the A–D labels for `[queued]`, the Transition selector and the status countdown. Next Beat/Bar/Phrase waits for a boundary; Manual waits for Trigger queued. An unavailable or stopped transport may hold the request. Trigger queued only activates a request captured in Manual mode. For a timed request, wait for valid timing or select Immediate and click the target again; Cancel queued abandons the request. Changing Transition **after** clicking does not change the policy of that queued request.

## Host timing is unavailable

Check the timing diagnostics below the top controls. Host PPQ requires valid PPQ; beat/bar boundaries can require a last-bar anchor. Automatic may fall back to valid sample timing plus manual tempo/meter. Sample Clock is specifically for sample-based fallback and explicit standalone free-run. If the DAW itself reports stopped, start transport.

## MIDI never changes the scene

The bottom line reports received note events; zero means no note events have been observed by the processor, not that MIDI is impossible. Route MIDI to the effect, match the assigned note number and check channel/host routing. Finish any preview before remapping notes. Read the queued-status line to distinguish MIDI delivery from transition scheduling.

## Preview buttons are unavailable or editing is locked

Preview is disabled during an existing preview. Hear stored/edited, Apply and Revert only enable while auditioning. The edit variation selector, MIDI mapping, and Undo/Redo are disabled during audition by design. Finish with Apply or Revert. Closing the editor also reverts a pending preview.

## Audio sounds unusual after seeking, changing tempo, or looping

The engine invalidates short-window history at detected seeks/loop wraps. A natural discontinuity in the host audio still exists; some transitions can be audible. PPQ-only discontinuities accompanying a simultaneous tempo change are a known detection limitation. Test the workflow in REAPER or your specific host.

## Where are audio import, visual step sequencing, morphing, and CLAP?

These are not available in this MVP editor. The current phrase strip is read-only. Expansion concepts exist as draft specifications on a separate branch and must not be described as shipped features.

## What to include with a bug report

Record OS, DAW and version, plugin type (VST3/Standalone), audio device, sample rate and buffer size, host tempo/meter, Timing method, Transition policy, steps to reproduce, whether MIDI counter increments, affected variation/operation and relevant log or crash file. Include a native screenshot that contains both the control involved and the timing/status label; scrub confidential project or file paths.

For build failures, capture full CMake configuration and compiler logs and check [Getting Started](../GETTING_STARTED.md).
