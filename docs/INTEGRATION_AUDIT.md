# Integration audit, 2026-10-12

## Scope and result

The Claude/Codex reconciliation preserves the substantive Claude scene/DSP implementation and the Codex PPQ boundary calculations. It replaces the alternative `SceneScheduler` with one `Scheduler` consumed by the processor, avoiding two competing processors and two conflicting scene owners. The prior M0 traceability checklist was stale: four scenes, five transformations, quantized transitions, MIDI, undo/audition, host state and bypass already have code and tests.

The requested selectable timing modes and editor completion are concurrent integration tasks. This audit does not certify their final merged implementation or a finished release. Expansion F01–F12 is a separate specification branch and outside this audit.

## Preserved behavior

| Area | Reconciliation evidence |
|---|---|
| Transport | Optional host tempo/meter/play/sample/PPQ inputs; manual field fallback; sample-domain phrase clock and PPQ boundary math |
| Scheduling | Immediate, beat, bar, phrase and manual policies; latest pending request; same-sample boundary support; fresh quantization through seeks/tempo changes |
| DSP | Repeat/stutter, reverse prior slice, gate, half-speed and tape-stop rate curve; preallocated history, causal reads, zero algorithmic latency |
| Scene ownership | Four independent named variations, sanitized operation settings, seed and configurable note mappings |
| UI/state handoff | Message-thread scene/undo model; immutable audio snapshot acquired with nonblocking try-lock; atomic requests/status |
| State | Stable APVTS IDs; embedded versioned scene bank; malformed scene recovery; oversized envelope guard |
| Performance controls | Host automation, MIDI timestamps, dry/wet, intensity smoothing and dry bypass ramp |

## Defect repaired by this audit

Captured audio survived a host seek or loop wrap. A reverse/repeat operation after jumping to a new section could therefore replay audio from the previous section. The processor now detects discontinuities using successive host sample positions. PPQ-only hosts use the prior block tempo and duration; the PPQ-only comparison is suspended during tempo changes to avoid misclassifying tempo ramps. The first block and unavailable position fields do not cause a reset.

The new `RhythmEngine::resetHistory()` invalidates capture in constant time. It resets history-relative time and read voices; negative history times read silence until new audio fills the needed window. It preserves parameter smoothing and does not allocate, clear megabytes of storage or wait on the callback. Full `reset()` remains available for prepare/test reset. Pending scene requests are preserved and the scheduler computes their boundary on the new timeline.

The core regression populates the ring with nonzero audio, invalidates history and confirms a subsequent reverse read does not replay it. Deliberately disabling the reset causes that test to fail. Added processor regressions cover both sample-position and PPQ-only seeks. They require the integration coordinator's final headless JUCE test run.

Limitations: host seeks inherently change source audio and can remain audible; this fix establishes history isolation, not an inaudibility guarantee. PPQ-only discontinuities occurring in the same block as a tempo change cannot be distinguished reliably from a host tempo ramp with the available metadata.

## Remaining specification discrepancies

| Contract | Actual implementation | Disposition |
|---|---|---|
| GM-FR-001 exposes editable phrase origin and beat subdivision | Manual tempo/meter/phrase bars; derived sample/PPQ origin; operation slice lengths | Concrete feature gap; do not advertise arbitrary grid-origin/subdivision authoring |
| GM-FR-002 independent rhythmic event data and visual lanes | Four independent uniform operation-setting groups, no arbitrary per-step event model | Existing MVP is an operation-settings model; event-lane sequencing needs a separate schema/scheduler specification |
| Slow-tempo history clamp visibly disclosed | DSP clamps effective repeat/reverse/rate slice lengths silently | UI must show the requested/effective limit or release notes disclose it; storage is bounded and tested |
| GM-FR-011 MIDI unavailable status | Note event count reports whether notes have arrived | Lack of notes does not establish that the host cannot route MIDI; phrase UI as “no MIDI notes received” |
| GM-FR-006 standalone timing fallback | Automatic no-playhead resolver uses manual fields but remains stopped | Explicit Sample Clock mode must provide a free-running operation path; automatic mode must not fabricate host playback |
| P0 UI audition/compare/undo/edit controls | Processor/core APIs exist; original editor only had intensity/status | Concurrent editor worker completes the controls; verify final combined UI in headless and real-host checks |
| GM-QR-004 seek behavior | History isolation repaired; existing fades handle operation wraps/changes | Audible source discontinuities need target-host listening and measured criteria |

## Static real-time review

`RhythmEngine::process` allocates no containers, performs no I/O and uses fixed-size operation/voice arrays. Ring memory is allocated in prepare. Processor callback uses existing buffer storage and chunks oversized host buffers. Model strings, serializer and undo allocations stay behind message-thread APIs. Audio snapshot handoff uses a try-lock and retains the prior scene on contention. Scheduler and clock calculations are bounded pure operations.

Parameter lookup and host transport adapter behavior require final integration review: repeated string-based parameter lookup can be replaced with cached raw pointers to reduce callback overhead. Actual framework callback allocation/lock instrumentation and stress testing remain necessary. Static review cannot prove deadline compliance.

## Tests and build evidence

Audit-local Linux GCC 13.3 Release portable build passed all seven CTest targets. These cover clock boundaries, optional transport resolution, scheduler policies, DSP fixtures/dry null/determinism/crossfades/history limits, scene isolation/sanitization, serialization recovery/round trips, MIDI note-map policy, undo and audition. The history-reset regression was observed failing with the reset disabled and passing with it restored.

No full JUCE binary, target DAW, audio device, Windows process, plugin validator, CPU budget or installation result was produced by this audit worker. Results from earlier PRs are historical until rerun on the final integration head. The coordinator should update verification evidence after timing, UI and this fix are combined.

## Release checks still required

REAPER Windows/Linux scan/load, transport start/stop/seek/loop/tempo/meter, actual MIDI routing, automation and save/reopen, mono/stereo/bypass/PDC, sample-rate and variable-block changes, UI keyboard/scaling, measured CPU/memory, allocation/lock instrumentation, clean-machine packaging and license notices remain acceptance work. Passing the portable suite establishes core fixture behavior, not beta readiness.
