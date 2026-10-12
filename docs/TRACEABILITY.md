# Requirement traceability

Reviewed against the approved MVP specification and continuation branch on 2026-10-12. The active implementation is still a timing/processor foundation, not a product-complete MVP. “Implemented” requires code, integration, and evidence on the same commit.

| Requirement | Current implementation | Verification evidence | Status / next action |
|---|---|---|---|
| GM-FR-001 phrase grid | PhraseClock supports sample and PPQ boundaries, stable phrase origin, meter, phrase length, and negative PPQ. TransportResolver carries host PPQ, last-bar PPQ, signed sample position, and fails closed for missing play state. No subdivision or explicit sync-mode UI. PPQ sample estimates use current tempo and must be refreshed every audio block. | New deterministic tests added for PPQ origin, odd-bar phrases, tempo-local calculation, PPQ-only transport and preroll. Current branch CI has not completed yet. | Partial. Add scheduler/display integration, sync modes and host seek/loop tests. |
| GM-FR-002 variations | No integrated scene model or UI yet. | None. | Not implemented. |
| GM-FR-003 transformations | No history or transform engine yet; callback still applies a gain ramp. | No transformation audio fixtures. | Not implemented. |
| GM-FR-004 transitions | No scene scheduler, queue, active/queued state, or transition mixer. | None. | Not implemented. |
| GM-FR-005 intensity/dry path | masterIntensity remains output gain, so zero still produces silence. | Legacy gain ramp test; current behavior violates the dry-at-zero contract. | Known defect. Replace with wet contribution and aligned dry path after the transform engine exists. |
| GM-FR-006 host timing | Host fields and PPQ reach the resolver; missing play state does not imply playback. No user-selectable Host/Manual/Free-run mode, loop policy, or scene scheduler. | Resolver tests extended for signed sample positions, PPQ-only hosts and unavailable fields. Current branch CI pending. | Partial. Add explicit mode selection and host discontinuity behavior. |
| GM-FR-007 automation | APVTS exposes intensity/manual tempo/meter/phrase-bars; raw parameter pointers are cached outside processBlock. No scene/transition automation map. | Headless APVTS tests exist; no REAPER automation test. | Partial. Freeze full stable-ID manifest and add scene controls/host test. |
| GM-FR-008 MIDI triggers | Plugin declares MIDI input but currently ignores messages. | None. | Not implemented. |
| GM-FR-009 audition/undo | No preview, compare, or undo/redo model. | None. | Not implemented. |
| GM-FR-010 state | APVTS scalar state round-trip; input is capped at 1 MiB, invalid state resets to factory parameter defaults, and an atomic recovery flag is displayed by the editor. No scene schema, version migrations, or transactional scene-state codec. | Processor tests cover scalar restore, malformed and oversized input, recovery flag, and defaults; current branch CI pending. | Partial. Add bounded versioned StateCodec after scene model stabilizes. |
| GM-FR-011 visible state | Editor contains intensity control, timing source/missing fields and state recovery text. No variation/phrase canvas or queued-state UI. | No UI automation/accessibility tests. | Partial. Implement the approved scene and transition workflow. |
| GM-FR-012 bypass/dry-wet | Processor declares zero latency; no transform mixer, explicit bypass, or dry/wet path yet. | Legacy layout and latency headless assertions only. | Not implemented beyond declaration. |
| GM-QR-001 real-time safety | APVTS atomic pointers are cached at construction; callback still calls host playhead once per block and publishes status atomically. Complete callback allocation/lock audit is pending. | Static review only; current branch tests pending. | Partial. Audit full call graph and instrument allocation/lock behavior. |
| GM-QR-002 resource use | No integrated DSP benchmark. | None. | Pending after transforms are integrated. |
| GM-QR-003 memory | No AudioHistory yet. | None. | Pending bounded-memory design and max-configuration measurement. |
| GM-QR-004 clicks | Gain transition now uses a fixed 1 ms sample-domain ramp, independent of host block size. No rhythmic transition or dry/wet residual check. | Processor test compares 480 rendered samples as one block and split 64/256/160 blocks. Current branch CI pending. | Partial. Add transition audio-quality fixtures. |
| GM-QR-005 timing | PPQ phrase clock and resolver tests added; future-tempo behavior requires a fresh host position/tempo query per block. No REAPER timing test. | Unit tests pending CI. | Partial. Integrate scheduler and test tempo maps, seeks, loops and block partitioning in hosts. |
| GM-QR-006 latency | Processor reports zero before transforms exist. | Headless latency assertion; no PDC test. | Pending transform integration and host PDC validation. |
| GM-QR-007 determinism | Timing math deterministic; no integrated DSP render test. | None. | Partial. Add repeatable renders after DSP exists. |
| GM-QR-008 reliability | No long-run stress test. | None. | Pending. |
| GM-QR-009 state recovery | Invalid state now defaults parameters and reports recovery. Scene-level versioning/migration is absent. | Headless tests added; current branch CI pending. | Partial. |
| GM-QR-010 accessibility | Recovery and timing use text; rest of MVP controls absent. | No keyboard/scaling test. | Pending UI milestone. |
| GM-QR-011 portability | PhraseClock/TransportResolver are portable standard-library modules; other DSP/state modules remain absent. | Prior run 38148457507 passed Windows/Linux core and plugin/Standalone jobs at older commit dfcabda. Current branch CI pending. | Partial. |
| GM-QR-012 privacy/security | No networking or content export path is present. .gitignore was malformed and is now replaced with build/IDE exclusions. Release dependency/license inventory remains open. | Source inspection and repository fix; no security scan or final dependency inventory. | Partial. |

## Build and QA evidence

- Run [38148457507](https://github.com/djshellshoxxx/GrooveMorph/actions/runs/38148457507) passed the four configured Windows/Linux core and plugin/Standalone jobs at dfcabda030f7e1432f4b51baad471b26ddc91f0c; this predates the continuation fixes.
- Run [38192285882](https://github.com/djshellshoxxx/GrooveMorph/actions/runs/38192285882) passed a Windows Standalone five-second launch smoke and artifact upload on the separate codex/m0-validation branch at 90e0a601e4ef4ffbc329ae112e8518695ca1382f. Artifact ID: 11722056502; it expires 2026-11-11. This validates the harness, not the current source branch.
- The current working branch is codex/mvp-continuation; draft PR [#2](https://github.com/djshellshoxxx/GrooveMorph/pull/2) targets main.
- No current-branch CI run, REAPER host test, physical audio-device test, timing alignment measurement, PDC/bypass check, listening test, 24-hour stress run, or release packaging has yet been verified.

## Remaining requirements and release gates

Implement the approved scene model, bounded history and five operations, transition scheduler/mixer, dry/wet path, trigger/automation mapping, versioned state and preview/undo; wire every visible UI control to those modules; then benchmark, optimize measured hotspots, run regression and native builds. Keep REAPER and physical device gates explicitly pending until actually executed.
