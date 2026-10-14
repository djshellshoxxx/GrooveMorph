# Known issues (alpha)

Ranked by severity.

1. **No real-host, audio-device or listening validation.** Everything audible is verified only by numerical/offline tests. Treat the sound quality of every effect, morph, placement boundary and the demo loop as unknown.
2. **Standalone transport defect (fixed in this build, unverified on hardware).** JUCE's Standalone wrapper reports a stopped transport and no tempo, which made every effect bypass permanently. This build ignores the wrapper transport and runs the free Sample Clock (Automatic = Sample Clock in Standalone). Found by reading JUCE source and covered by a unit test; not confirmed on a Windows audio device.
3. **Event phase.** Events gate an effect; they do not restart its internal cycle (Repeat 4-slice cycle, Gate half-slice). An event that starts mid-cycle starts mid-cycle.
4. **Locks protect from generation only.** Manual edits to locked fields are still possible.
5. **Meter/phrase length change.** Events keep their tick positions. Events past a shortened phrase wrap around; there is no conflict badge or "scale events" command.
6. **Host automation.** Slice/placement/limits are scene state, not host parameters. Morph/auto-return/variation are parameters and are block-observed (not sample-accurate). A host in automation-write mode may record the Variation realignment.
7. **Generation history is session-only** (not saved with the project).
8. **Block-observed controls apply on slider release** for scene sliders (depth/slice/placement), to keep one undo step per drag.
9. **Help pages** under `help/` predate the new controls.
10. **Linux UI screenshots only.** Screenshots in `docs/alpha/screenshots/` were rendered by the project's own snapshot tool (software rendering of the real editor), not captured from a Windows desktop.
11. Spec-only / absent features are listed in ALPHA-FEATURE-STATUS.md.
