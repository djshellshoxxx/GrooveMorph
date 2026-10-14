# Alpha feature status

Windows x64 Standalone **alpha** (exploratory; not a beta, not production). Status words are used
strictly: *Complete and tested* / *Implemented, partially tested* / *Partially implemented* /
*GUI-only / not functional* / *Specification only* / *Not implemented* / *Blocked by dependency*.
"Tested" means automated tests on Linux (and Windows CI for portable/processor tests). No feature was
tested in a DAW, with audio hardware, or by listening.

| Feature | Status | What works | What is unfinished | Known defects | Next action |
|---|---|---|---|---|---|
| Original runtime (4 scenes, 5 effects, Scheduler, MIDI, audition, undo, state) | Implemented, partially tested | Unchanged canonical engine; all pre-existing tests still pass | Native host/device QA (issue #19) | See KNOWN-ISSUES | Issue #19 |
| Standalone free-run clock | Implemented, partially tested | Standalone ignores the wrapper's transport and runs the Sample Clock, so effects actually engage without a host | Not verified on a real Windows audio device | Previously, Standalone effects could never engage (see KNOWN-ISSUES) | Verify on hardware |
| A01 Phrase placement + event lanes | Implemented, partially tested | Per-scene window (full/custom/last bar), every-N-phrases cadence, origin, snap grid, downbeat protect; per-effect events (max 32/scene) with depth, drag/resize/nudge/delete, convert-to-events; sample-accurate boundaries at any block size; scene-isolated; legacy states load unchanged | Event-local slice/phase restart/cycle anchors; event priority; 256-event capacity; split-at-boundary command; scale-events/conflict badge on meter change; GUI interaction tests | Effects keep shared beat phase inside events (event starting mid-cycle starts mid-cycle) | Add per-operation phase targets to the engine (C01 spec) |
| A02 Host Variation parameter sync | Implemented, partially tested | Parameter = requested variation; realigned to the sounding scene after completion/cancel/GUI/MIDI/auto-return; never written from the audio thread; echo ignored by audio | Real-host automation/recorder behaviour (undo entries, write modes) | A host in write mode may record the realignment | Test in REAPER |
| A03 Variation generator | Implemented, partially tested | Seed, style, distance, candidate/regenerate/previous, never-touch ops, locks, history (session), isolated preview, Apply/Revert/Undo, deterministic reproduction | History not saved; no candidate list view; GUI interaction not automated | None known | Persist history; candidate gallery |
| A04 Scene management | Implemented, partially tested | Duplicate whole scene, Copy FX, reset scene, reset effect, clear placement, rename, undo/redo, sounding/queued/editing indicators, safe preview/compare | Fade policy; stable operation automation IDs | None known | Native host check |
| C01 Phrase placement (spec) | Partially implemented | See A01 | See A01 | See A01 | See A01 |
| C02 Variation distance | Partially implemented | Distance 0..1 blends source toward candidate (depth linear, slice log, enable flip >= 0.5) | Spec's richer distance metric and per-style limits | None known | Define a distance metric |
| C03 Change permissions | Partially implemented | Per-effect locks for Enable/Depth/Slice/Events and placement lock, enforced after generation | Per-event locks, protected temporal regions, Protect first beat region | Manual edits are not blocked by locks (locks protect from generation only) | Add regions + manual-edit policy |
| C04 Transition phase | Specification only | - | Everything | - | Extend Scheduler phase policy |
| C05 Automatic return | Partially implemented | Return after 1-32 bars to previous or A-D, lands on the bar line (tested sample-accurate); cancelled by new request, Cancel, seek | Phrase/time units, dry target, cancel UI state beyond status text | None known | Phrase units + Dry target |
| C06 Activity limits | Partially implemented | Max effects at once (1-5) with per-effect priority, applied live | Minimum event spacing, maximum phrase coverage, Analyze/Reduce actions | None known | Spacing/coverage |
| F01 Controlled generation | Partially implemented | Delivered by A03 | Pattern-level (event) generation | - | Generate events |
| F02 Scene morphing | Partially implemented | Host-automatable morph target + amount blends the sounding scene's settings toward another scene (depth/slice interpolation, one-sided effects fade) | Morph of placement/events; audio-level crossfade; morph preview in editor | Large slice changes still rely on the engine's 3 ms crossfades | Listening tests |
| F03 Groove/downbeat/LF protection | Partially implemented | Downbeat protection (silence effects for 0-4 beats at each bar start) | Low-frequency protection (needs band split) | - | Band-split design |
| F04 Performance mode | Not implemented | Intensity, Dry/Wet, Morph amount, scene buttons are usable as manual macros | Macro mapping, performance layout | - | Spec review |
| F05 Phrase evolution | Not implemented | - | Everything | - | After A03 history is persisted |
| F06 Polyrhythmic lanes | Not implemented | - | Per-lane clocks | - | Needs per-operation phase targets |
| F07 Audio-triggered actions | Not implemented | - | Onset detection/triggers | - | Spec review |
| F08 Variation generation history | Partially implemented | Session history of the last 24 candidates, exact recall | Persistence, compare view | History is lost on editor/project close | Persist in state |
| F09 Audio import/capture | Not implemented | Built-in demo drum loop only (Standalone evaluation aid) | Import, capture, waveform | - | Spec review |
| F10 Multiband | Not implemented | - | Everything | - | After F03 LF protection design |
| F11 Gesture recording | Not implemented | - | Everything | - | Spec review |
| F12 Expanded MIDI / formats | Not implemented | Existing MIDI scene mapping unchanged; VST3 + Standalone only | CLAP, additional MIDI features | - | Deferred by project scope |
| Demo loop | Implemented, partially tested | Deterministic kick/snare/hat loop, seek-invariant (unit tested), replaces input while the transport plays | Not audibly verified by the author | Level/tonality unreviewed | Listen |
| Windows Standalone alpha package | See BUILD-MANIFEST.md | Built and packaged by `windows-alpha.yml` | No visual GUI test on a real Windows desktop by the author | - | Manual evaluation |

Deviations from `docs/control-specs`: events use meter-beat ticks (960/beat) instead of quarter-note ticks;
scene-control keys are optional keys in format v1 rather than a v2 header; capacity is 32 events/scene.
