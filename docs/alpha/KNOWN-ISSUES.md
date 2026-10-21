# Known issues (alpha 2)

Ranked by severity.

1. **No real-host, audio-device or listening validation.** Everything audible is verified only by numerical/offline tests. Treat the sound quality of every effect, morph, placement boundary, band split, loop and the demo loop as unknown.
2. **Standalone transport (fixed, unverified on hardware).** JUCE's Standalone wrapper reports a stopped transport, which used to bypass every effect. The wrapper's transport is now ignored and the free Sample Clock runs ("Automatic" = Sample Clock in Standalone). Covered by a unit test; not confirmed on a Windows audio device.
3. **Sample rates** below 8 kHz or above 384 kHz are rejected by the timing code, so the effects stay bypassed and variation switches do not fire. (The limit was 192 kHz until 2026-10-14.)
4. **Processor timer.** Host-parameter realignment, MIDI macros, loop publishing, gestures and evolution run on a message-thread timer started in the constructor. A host that creates the plugin off the message thread would not run it, and destroying the plugin on a non-message thread while the timer fires is not guarded.
5. **Meter/phrase length change.** Events keep their tick positions; events past a shortened phrase wrap around (drawn wrapped) and there is no conflict badge. Use Limit > Events x2 / /2 to rescale (it drops events that would pass the phrase end). Overlap checks follow each lane's loop ring; Reduce still works on the phrase ring.
6. **Capture/loop limits.** 20 s maximum; capture refuses lengths that would exceed it and stops if the transport stops; a tempo change (more than 0.1 %) or meter change during recording now aborts the capture with a message. Loop audio is saved with the project (16-bit, up to 20 s; a loop whose compressed form exceeds 10 MiB is not saved and a warning is shown). Generator history is saved locally on this computer, not in the project. The gesture curve/target, evolution settings and MIDI links are saved with project state. Imported loops follow the tempo by varispeed (pitch follows) unless Input > "Stretch to host tempo" is on (WSOLA, off by default, listening quality untested).
7. **Band split** defaults to a first-order crossover (6 dB/oct). The 12 dB/oct option steepens only the band sent to the effects; the other band is its complement, so the overlap around the crossover remains audible. Not Linkwitz-Riley.
8. **Audio trigger / macros** use a simple detector and a ~20 Hz parameter write rate; expect false or missed triggers on dense material.
9. **Host automation.** Slice/placement/limits are scene state, not host parameters (beta: per-effect Enable/Depth/Slice can be automated as live trims on the sounding scene, `op<N>Enable/Depth/Slice`; stored scenes are not automatable). Morph/auto-return/band/trigger/variation are parameters, block-observed. A host in automation-write mode may record the Variation realignment.
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

## Full bug check of PR #28 (2026-10-14)

Three read-only audits plus ASan/UBSan/TSan core runs and clang-tidy found and I fixed: ticking "Lock event" deleted the event and un-ticking duplicated it (lock flags are now lock configuration; new events over a locked one are dropped); the old scene's last samples before a switch used the new scene's phase; the demo/loop source and loop capture followed the shifted pattern beat; a seek reset the phase offset to 0 instead of the active scene's offset (also re-resolved on meter/phrase change, reset and prepare); a Restart request was lost when no timing was available; gesture playback and the Variation echo were sent to the host as change gestures every 50 ms and the gesture lock was held while calling the host; stale crossover filter state after a mode/slope change; evolution distance overflow on hostile state; the added protected region was not selected; the gesture text format depended on the C locale.

Known and not fixed: C04 reads the phase configuration at switch time, not request time (editing the scene between request and launch changes the launch); the editor fit uses the primary display only and a host-applied scale factor overrides it; the crossover coefficient is not smoothed against fast automation; a non-finite input sample can still reach the output through the dry path; morph ignores the target scene's phase, regions and locks.

15. See [AUDIT-2026-10-14.md](../AUDIT-2026-10-14.md) for the full list of fixed and still-open findings from the first 2026-10-14 code audit.

## Full code and feature audit of the merged line (2026-10-14)

Four independent read-only audits (processor/audio thread, core/state, UI/Visual window, features vs docs) plus ASan/UBSan and TSan core runs. Fixed: the Visual planner and rings ignored the C04 phase shift; trace beats were non-monotonic across a shift or a transport stop (epochs now start on those); Stop/rewind kept the phase shift; meters of 17-32 beats per bar were clamped to 16 on the UI side; planner windows could be skipped by a 1e-12-beat rounding error (positions snap to ticks); locks could be bypassed through loop lengths and the explicit-events switch; Reduce let disabled events block audible ones; the serializer flagged valid protected regions as "recovered"; an oversized loop load silently truncated; the importer's resampler read past its buffer; Visual-window tooltips never showed; the "Enter exact value" dialog could outlive its editor; disabled controls offered the right-click menu; non-current inspector tabs were unnamed and untested for tooltips; typed numbers did not accept 1e-3 or 1/8; MIDI learn stayed armed after the editor closed; host-parameter exact values skipped the automation gesture; the Evolve toggle stayed ticked after Reset; the seed field reset the candidate number; right-clicking the lane canvas created events; stale text in the Info tab, user test guide and help pages.

Known and not fixed:
- Undo can revert the sounding scene or live (MIDI-driven) edits made since the last committed edit; wheel/keyboard slider steps are one undo entry each (64 deep).
- MIDI learn links the lowest-numbered CC that moved in a 50 ms tick, fails silently when the 64-link table is full, and CC pulses faster than the 20 Hz poll can be missed; `s:` links are dropped while previewing.
- Evolution steps land up to ~50 ms after the phrase boundary.
- Sample rates outside 8 kHz-384 kHz disable the processor silently; buffers at 384 kHz are about 235 MB; the trace ring covers under 3 s at 384 kHz so the Visual window shows little heard history there.
- LaneCanvas: events narrower than ~12 px can only be moved with the arrow keys; a double-click on an empty lane can create then delete; the Visual window's Weave/Ledger views are cramped at the 900x560 minimum; the Scenes tab claims success even when a lock reverted the change.
- Accessibility: editor sliders/combos have no accessible names beyond tooltips; the Visual views update descriptions at 30 Hz.
- `morph()` takes lane events from scene A only; a held MIDI note can stay stuck after a panic until `reset()`.
- The release job also runs when a pushed commit message contains `[publish-alpha]`; the binary reports version 0.1.0 while tags carry the run number; no LICENSE or third-party (JUCE) notice ships with the ZIP.


## Beta update, workflow and automation worker W10 (2026-10-20)

Fixed in code (core pieces have tests; the editor and processor parts need a JUCE build and have not been run on the authoring machine):
- A scene note can no longer be silently doubled: a conflicting assignment asks Swap or Cancel (one Undo step). Saved maps with duplicates still load and keep first-match dispatch.
- MIDI learn links the CC that moved most, notices pulses that return to their start value between two polls, and reports a full 64-link table (status line, message box, greyed menu item).
- CC 120 and CC 123 release a held trigger note (the "stuck after panic" issue).
- Per-operation host automation: `op<N>Enable/Depth/Slice` live trims for every operation, default identity (spec `docs/specs/W10-OP-TRIMS.md`).
- Lane canvas: keyboard cursor and Enter creates events, thin events move on a plain drag (Shift/Alt resize), a badge counts events past the phrase end.
- Scenes tab no longer claims success when a lock kept fields; slider steps within 500 ms share one undo entry; sliders, combos, buttons and text fields have accessible names and descriptions; Visual window descriptions update at most 4 Hz.

Still open: linked CCs and macros are still applied by the 20 Hz timer (documented in `docs/specs/W10-MIDI-PANIC-RATES.md`); the Visual window draws the untrimmed scene; `s:` links are still ignored during a preview; no screen-reader, DAW or hardware test.
