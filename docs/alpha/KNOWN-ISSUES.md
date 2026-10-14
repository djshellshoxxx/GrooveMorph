# Known issues (alpha 2)

Ranked by severity.

1. **No real-host, audio-device or listening validation.** Everything audible is verified only by numerical/offline tests. Treat the sound quality of every effect, morph, placement boundary, band split, loop and the demo loop as unknown.
2. **Standalone transport (fixed, unverified on hardware).** JUCE's Standalone wrapper reports a stopped transport, which used to bypass every effect. The wrapper's transport is now ignored and the free Sample Clock runs ("Automatic" = Sample Clock in Standalone). Covered by a unit test; not confirmed on a Windows audio device.
3. **Sample rates above 192 kHz** are rejected by the existing timing code, so the effects stay bypassed and variation switches do not fire (e.g. a 384 kHz session). Audio is passed through unchanged.
4. **Processor timer.** Host-parameter realignment, MIDI macros, loop publishing, gestures and evolution run on a message-thread timer started in the constructor. A host that creates the plugin off the message thread would not run it, and destroying the plugin on a non-message thread while the timer fires is not guarded.
5. **Meter/phrase length change.** Events keep their tick positions; events past a shortened phrase wrap around (drawn wrapped) and there is no conflict badge or "scale events" command. The overlap check ignores per-lane loop rings, so a looped lane can contain overlapping events (the first one wins).
6. **Capture/loop limits.** 20 s maximum; capture refuses lengths that would exceed it and stops if the transport stops; a tempo change during recording misaligns the loop length. Loop audio, gestures, evolution and history are not saved with the project. Imported loops are not time-stretched (pitch follows tempo).
7. **Band split** is first-order (6 dB/oct): gentle, with audible overlap around the crossover.
8. **Audio trigger / macros** use a simple detector and a ~20 Hz parameter write rate; expect false or missed triggers on dense material.
9. **Host automation.** Slice/placement/limits are scene state, not host parameters. Morph/auto-return/band/trigger/variation are parameters, block-observed. A host in automation-write mode may record the Variation realignment.
10. **Scene sliders apply on release** (depth/slice/placement) to keep one undo step per drag.
11. **Window larger than small screens.** Default 1290x740, minimum 1230x700. On the 1024x768 CI desktop the window is cropped (confirmed in the Windows screenshot). Use a display of at least about 1300x800.
12. **Screenshots:** Linux-rendered via the project's snapshot tool (software rendering of the real editor); the only Windows capture is the CI startup screenshot.
13. **Help pages** under `help/` predate the new controls.
14. Remaining unimplemented items (C04, protected temporal regions, per-event locks, steeper crossover, CLAP) are listed in ALPHA-FEATURE-STATUS.md.

## Audit 2 (2026-10-14): fixed vs. still open

Fixed in PR #26 (core, verified by new regression tests and CI): scene files dropped TapeStop (operation 4)
permission/activity/lane keys; history operations replayed silence after a transport reset; NaN/Inf input could
poison the history ring; bypass was not bit-exact for tiny values; planner seed and candidate index aliased; audition
commit kept a stale original; a `const_cast` on an atomic CAS expected value (UB); parameters missing from restored
state kept the previous session's values.

Still open (not fixed, not hidden):
- A seek or reset produces one step from the effect output to the dry signal (inherent without a crossfade).
- Drag-created event lanes: an audit claim that created events keep a default length could not be reproduced by code
  reading; needs a hands-on GUI check.
- Band-split NaN / wet-0 handling, change gestures, lazy timer start, capture abort on seek, grid clamp 32.
- GUI polish items from the audit, compact-layout scale factor, 384 kHz sample-rate limit.
- C04 transition phase, protected regions / per-event locks, 12 dB crossover slope, persistence of gesture/evolution
  state, scale-events command.
- No software licence is declared; the owner must choose one.
- No DAW, audio-device or listening tests have been performed.
