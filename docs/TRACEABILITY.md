# Requirement traceability

Reviewed against the approved MVP specification on 2026-10-12. The active implementation at dfcabda is a timing/processor spike, not an MVP release. “Implemented” requires code, integration, and evidence; the presence of a type or parameter stub is not completion.

| Requirement | Current implementation | Verification evidence | Status / next action |
|---|---|---|---|
| GM-FR-001 phrase grid | PhraseClock computes meter-beat/bar/phrase sample offsets. Processor exposes tempo, meter, phrase-bars. No subdivision/sync mode UI or PPQ origin. | PhraseClock tests pass; CI run 38148457507 succeeded on Windows/Linux, including plugin/Standalone build. | Partial. Add canonical PPQ, last-bar origin, subdivision/sync policy, negative pre-roll tests. |
| GM-FR-002 variations | No scene model; OperationParams is an unused settings stub. | None. | Not implemented. Add four stable-ID independent scenes and model tests. |
| GM-FR-003 transformations | No history or transform engine. Processor applies a gain ramp. | No operation audio tests. | Not implemented. Add bounded history and deterministic DSP. |
| GM-FR-004 transitions | No scene scheduler, queue, active/queued state, or transition mixer. | None. | Not implemented. |
| GM-FR-005 intensity/dry path | masterIntensity is output gain; zero gives silence. | Gain ramp test only. | Known defect. Convert intensity to wet contribution with aligned dry output. |
| GM-FR-006 host timing | TransportResolver accepts tempo/meter/play/sample position; editor displays source/missing fields. PPQ/loops/seek policy absent; missing play state currently defaults to playing. | TransportResolver unit tests and processor mock-playhead tests pass in CI run 38148457507. | Partial. Implement explicit Host/Manual/Free-run policy; unknown play state; PPQ/bar origin and discontinuity handling. |
| GM-FR-007 automation | APVTS exposes intensity and manual timing parameters; no scene/transition parameters. IDs exist for current five scalar controls. | Headless parameter state tests pass in CI; REAPER automation not run. | Partial. Freeze ID manifest; add stable discrete scene/transition mapping and host test. |
| GM-FR-008 MIDI triggers | Plugin advertises MIDI input but ignores MIDI buffer. | None. | Not implemented. Add timestamped command routing and host routing tests. |
| GM-FR-009 audition/undo | No preview model, compare, or undo/redo. | None. | Not implemented. |
| GM-FR-010 state | APVTS scalar state round-trip; no model/schema/migration; malformed input is silently ignored. | Headless scalar round-trip and malformed-state check pass in CI, but malformed recovery contract is wrong. | Partial with known defect. Add capped, transactional versioned StateCodec and defaults/diagnostic recovery tests. |
| GM-FR-011 visible state | Editor has intensity slider and timing source/missing fields only. | No UI automation/accessibility tests. | Partial. Add scene/queue/status UI; capture/analysis status remains deferred unless features are added. |
| GM-FR-012 bypass/dry-wet | No internal dry/wet or explicit bypass contract; zero algorithmic latency is set. | Headless latency/layout checks pass; no host PDC or audio bypass QA. | Not implemented beyond zero-latency declaration. |
| GM-QR-001 real-time safety | Callback reads APVTS through per-block ID lookup, reads playhead each block, updates atomics, applies gain ramp. No instrumented allocation/lock test. | Static source review only; no allocation/sanitizer run. | Partial and not release-verified. Cache parameter pointers; inspect JUCE host calls and instrument later. |
| GM-QR-002 resource use | No DSP performance benchmark. | None. | Pending M2 benchmark. |
| GM-QR-003 memory | No history/scratch allocation model yet. | None. | Pending AudioHistory design and max configuration measurement. |
| GM-QR-004 clicks | Gain uses block ramp; no transition residual test. | One ramp endpoint check. | Partial. Define and run transition measurement protocol. |
| GM-QR-005 timing | PhraseClock math and resolver fixtures exist; tempo-map phase remains wrong without PPQ. | Unit tests only; no REAPER sample alignment. | Partial. Implement PPQ and add host test. |
| GM-QR-006 latency | Processor reports zero. No operation path to measure. | Processor headless latency test passes; no PDC host test. | Partial. Reverify after DSP/wet path. |
| GM-QR-007 determinism | Timing tests deterministic; no output-level DSP fixtures. | Core tests pass. | Partial. Add repeatability/reset/seek render tests. |
| GM-QR-008 reliability | No long-running stress. | None. | Pending 24-hour run before full beta acceptance. |
| GM-QR-009 state recovery | Existing malformed state is ignored, leaving prior state. | Test only asserts malformed input ignored. | Fails contract. Fix after spec review and update tests. |
| GM-QR-010 accessibility | Basic labels exist; no core workflow or scaling/focus test. | None. | Pending UI milestone. |
| GM-QR-011 portability | Core clock and resolver are standard-library-only; other required DSP/state modules absent. | Linux/Windows CI core and plugin build passed at exact commit dfcabda. | Partial. Keep core targets JUCE-free and test as modules are added. |
| GM-QR-012 privacy/security | No network features in current source. No release dependency inventory. | No sockets observed in source reviewed; no dependency/license release audit. | Partial. Add dependency/license inventory before packaging. |

## Integration gates

- Current CI evidence: workflow run [38148457507](https://github.com/djshellshoxxx/GrooveMorph/actions/runs/38148457507) for exact head dfcabda030f7e1432f4b51baad471b26ddc91f0c passed. This verifies configured CI build and tests only; it does not establish REAPER, physical audio-device, audio-quality, or distribution acceptance.
- Remaining host checks: REAPER Windows/Linux VST3 scan/load, transport/tempo map, automation, MIDI, state save/reopen, bypass/PDC, offline render, standalone audio-device flow.
- Planned validation branch: codex/mvp-continuation; merge to main only after the user-requested spec review, defect fixes, optimization pass, QA fixes, and final acceptance decision.
