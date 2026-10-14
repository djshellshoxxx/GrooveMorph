# Known issues (alpha 2)

Ranked by severity.

1. **No real-host, audio-device or listening validation.** Everything audible is verified only by numerical/offline tests. Treat the sound quality of every effect, morph, placement boundary, band split, loop and the demo loop as unknown.
2. **Standalone transport (fixed, unverified on hardware).** JUCE's Standalone wrapper reports a stopped transport, which used to bypass every effect. The wrapper's transport is now ignored and the free Sample Clock runs ("Automatic" = Sample Clock in Standalone). Covered by a unit test; not confirmed on a Windows audio device.
3. **Sample rates above 192 kHz** are outside the documented timing contract (8-192 kHz): the effects stay bypassed and variation switches do not fire (e.g. a 384 kHz session). Audio is passed through unchanged. Raising the limit needs a specification change.
4. **Processor timer.** Host-parameter realignment, MIDI macros, loop publishing, gestures and evolution run on a message-thread timer started in the constructor. A host that creates the plugin off the message thread would not run it, and destroying the plugin on a non-message thread while the timer fires is not guarded.
5. **Meter/phrase length change.** Events keep their tick positions; events past a shortened phrase wrap around (drawn wrapped) and there is no conflict badge. Use Limit > Events x2 / /2 to rescale (it drops events that would pass the phrase end). Overlap checks follow each lane's loop ring; Reduce still works on the phrase ring.
6. **Capture/loop limits.** 20 s maximum; capture refuses lengths that would exceed it and stops if the transport stops; a tempo change during recording misaligns the loop length. Loop audio and history are not saved with the project (the gesture curve/target and evolution settings are). Imported loops are not time-stretched (pitch follows tempo).
7. **Band split** defaults to a first-order crossover (6 dB/oct). The 12 dB/oct option steepens only the band sent to the effects; the other band is its complement, so the overlap around the crossover remains audible. Not Linkwitz-Riley.
8. **Audio trigger / macros** use a simple detector and a ~20 Hz parameter write rate; expect false or missed triggers on dense material.
9. **Host automation.** Slice/placement/limits are scene state, not host parameters. Morph/auto-return/band/trigger/variation are parameters, block-observed. A host in automation-write mode may record the Variation realignment.
10. **Scene sliders apply on release** (depth/slice/placement) to keep one undo step per drag.
11. **Small screens.** Default 1290x740, minimum 1230x700. The editor now scales itself down to fit the primary display's usable area (not yet confirmed on a Windows machine; the earlier 1024x768 CI screenshot showed it cropped).
12. **Screenshots:** Linux-rendered via the project's snapshot tool (software rendering of the real editor); the only Windows capture is the CI startup screenshot.
13. **Help pages** under `help/` predate the new controls.
14. Remaining unimplemented items (per-scene transition smoothing time, Linkwitz-Riley crossover, loop-aware Reduce, CLAP) are listed in ALPHA-FEATURE-STATUS.md.

## Audit 2 (2026-10-14): fixed vs. still open

Fixed in PR #26 (core, regression tests, CI green): scene files dropped TapeStop (operation 4) keys; history operations replayed silence after a transport reset; NaN/Inf input could poison the history ring; bypass was not bit-exact for tiny values; planner seed/candidate aliasing; audition commit kept a stale original; a `const_cast` on an atomic CAS expected value; missing parameters kept the previous session's values on state restore.

Fixed afterwards (this round, tests added): band split is bit-exact at wet 0 and NaN-safe; a recording that spans a host seek is aborted; plugin-originated parameter writes are change gestures; the editor shrinks to small displays; C04 scene start phase, per-event locks, protected regions, scale-events command, loop-ring-aware overlap, 12 dB/oct crossover option, saved gesture/evolution state.

Still open (not fixed, not hidden):
- A seek or reset produces one step from the effect output to the dry signal (inherent without a crossfade).
- Drag-created event lanes: an audit claim that created events keep a default length could not be reproduced by code reading; needs a hands-on GUI check.
- The processor timer needs a message thread at construction; destruction off the message thread is not guarded (the timer starts only when a message manager exists).
- Per-scene transition smoothing time (C04), loop-aware Reduce, Linkwitz-Riley crossover.
- No software licence is declared; the owner must choose one.
- No DAW, audio-device or listening tests have been performed.
