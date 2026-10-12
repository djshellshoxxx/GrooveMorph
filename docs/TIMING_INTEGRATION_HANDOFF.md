# Canonical GrooveMorph timing integration

The canonical runtime is the Claude scene/DSP/host-state implementation with the
portable Scheduler extended by the Codex PPQ calculations. The Codex M0 processor
and alternate SceneScheduler must not overwrite this runtime. Both timing methods
are selectable through the appended stable `timingMode` APVTS choice and editor
attachment, and share the same transformations, scenes and transition policies.

| Value | Mode | Quantization and operation phase | Missing host context |
|---|---|---|---|
| 0 | Automatic | Prefer fresh PPQ; quantization falls back to sample grid | Manual BPM/meter replace invalid host fields; missing play state stays stopped |
| 1 | Host PPQ | Require PPQ; beat/bar boundaries require a credible last-bar anchor; phrase anchor is originPpq | Quantized requests remain armed; missing PPQ makes rhythmic processing dry; Immediate/Manual still work |
| 2 | Sample Clock | Ignore PPQ origins; use host sample position or internal sample counter at current tempo | Missing play state permits explicit standalone free-run; an explicitly stopped host remains stopped |

Older host state without timingMode restores Automatic even on an existing
instance. Pending requests retain the transition policy selected at request time;
changing the timing mode recomputes their boundary using the new clock. Boundaries
are rounded to the nearest sample, including a callback starting just beyond an
exact fractional PPQ boundary. MIDI and render splits advance both sample and PPQ
positions. Host tempo ramps within one callback are limited by the single JUCE
transport snapshot, not interpolated without evidence.

Host seeks and loop wraps invalidate historical audio in constant time. No second
scheduler is active. Main engineering skills/instructions are preserved. Scene
name, operation, MIDI mapping, undo/redo and audition controls are now connected.
The editor exposes independent scene editing and queued/active state. Slice
controls disclose bounded history limits.

## Verification

Local Linux GCC 13.3 Release: VST3 and JUCE Standalone built; all 8 CTest targets
passed. Portable tests cover clocks, scheduling, DSP, scenes, state and undo. The
processor test additionally covers divergent clock selection, mode-state recall,
legacy mode migration, Sample Clock preview, MIDI sub-block alignment, DSP phase
independence from unrelated MIDI splits, sample/PPQ-only seek isolation and real
editor callbacks for timing selection and isolated preview/revert/close behavior.
The editor was software-rendered and visually inspected.

Latest CI and merged commit references will be appended once verified. REAPER
Windows/Linux listening, actual devices, VST3 validator, deadline/allocation
instrumentation and clean-machine install remain unavailable in this session.
The integration is not a claim that all original release acceptance gates pass.

## Post-integration audit and ownership

See INTEGRATION_AUDIT.md and TRACEABILITY.md. Explicit editable phrase origin and
beat subdivision, arbitrary event lanes, target hardware performance and the
independent F01–F12 expansion package remain separate work. Expansion contracts
must pin this canonical timing/scene interface before implementation. PR #2 is recorded as a reconciled ancestor after selective integration; preserve
its branch as historical source; its useful clock/state/CI contracts were
selectively reconciled rather than merging its older processor over this engine.

A local Linux Standalone process smoke attempt was unavailable because the managed
environment could not open Xvfb listening sockets. The software-rendered editor
and callback tests passed; native Windows startup is validated by CI.
