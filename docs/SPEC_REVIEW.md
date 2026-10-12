# Specification review and implementation gap audit

**Reviewed:** 2026-10-12  
**Specification:** docs/SPECIFICATION.md, approved MVP overlay dated 2026-10-10  
**Code reviewed:** active branch claude/awesome-keller-jx2ck0, commit dfcabda030f7e1432f4b51baad471b26ddc91f0c  
**Review method:** requirements-to-code/test traceability, architecture and failure-path consistency, independent timing/state and architecture reviews.

## Finding summary

The specification covers the requested product areas and correctly defers full phrase capture, pitch-preserving stretch, CLAP, multiband processing, random generation, and unrelated effects. The present code remains an M0/M1 timing and gain spike. A green build is not feature completion.

The main document had four meaningful contract conflicts. They are resolved in the normative §5.1 amendment:

1. Host sample-position plus one current BPM cannot preserve musical phase through tempo maps or bar offsets. The contract now prefers signed PPQ and host bar-start PPQ; sample-phase fallback must be labeled.
2. Missing host play state was treated as playing, while the spec required explicit free-run/preview behavior. Missing play state is now Unknown; Free-run requires explicit selection/arming.
3. Malformed state behavior was inconsistent: current tests/code ignored invalid state, while the spec required default recovery. The contract now caps input at 1 MiB, parses transactionally, loads safe defaults on invalid input, and surfaces recovery status.
4. P0 UI status required capture/analysis feedback even though capture and analysis are deferred. MVP status only describes shipped controls; capture/analysis status applies only if those features are later enabled.

The review also aligned the supported meter numerator to the portable grid's 1–32 range and defined the MVP subdivision set. Remaining provisional CPU/memory/click thresholds require actual benchmark hardware and measurement before they are frozen.

## Mandatory implementation gaps at reviewed commit

| Priority | Requirement area | Evidence at reviewed commit | Required next work |
|---|---|---|---|
| P0 | FR-001 / QR-005 phrase grid | Timing math exists; no PPQ/bar-start, subdivision or sync-mode control. | Add PPQ and discontinuity context and tests for tempo maps, bar offsets, seeks, loops, negative pre-roll. |
| P0 | FR-002 scenes | No scene model; operation settings are unused. | Four stable-ID scenes, independence and validation tests. |
| P0 | FR-003 transformations | No audio history or DSP operations; callback only applies gain ramp. | Preallocated history and repeat, reverse, gate, half-time, rate-ramp DSP with deterministic fixtures. |
| P0 | FR-004 transitions | No scheduler, queue, active/queued state or mixer. | Fixed-capacity event scheduler and deterministic conflict/overflow semantics. |
| P0 | FR-005 / FR-012 dry/wet | Master intensity is output gain; zero intensity is silence; no explicit bypass/mix alignment. | Convert intensity to wet blend; dry at zero, measured latency/bypass alignment. |
| P0 | FR-006 host timing | Resolver exists, but no explicit sync mode; missing play state currently means playing. | Implement amended source precedence and Unknown/stopped/armed behavior. |
| P0 | FR-007 automation | Only scalar timing/intensity parameters exist. | Freeze existing IDs; add scene and transition automation mapping and host tests. |
| P0 | FR-008 MIDI | MIDI is advertised but ignored. | Sample-offset note mapping, note-off policy, collision order and fallback tests. |
| P0 | FR-009 audition/undo | No preview model or undo/redo. | Temporary preview snapshot, bounded undo model and exact restore tests. |
| P0 | FR-010 state | Scalar APVTS state only; no cap/schema/migration/default recovery diagnostic. | Versioned model codec and invalid-state transaction tests. |
| P0 | FR-011 UI | Only intensity slider and timing text exist. | Scene strip, phrase grid, active/queued state, trigger/sync status and accessible control feedback. |
| QR gates | QR-001–012 | No callback allocation instrumentation, transition/audio-quality, performance, memory, long-run, accessibility, packaging or dependency-license evidence. | Complete tests and measurement according to §5.2 and §7; record unavailable host checks precisely. |

## Spec audit decisions and limitations

- Keep full capture, analysis, multiband, random generation, sidechain/audio triggering, and scene-bank import/export deferred for MVP.
- Keep the proposed CPU, memory, click, timing and stress targets provisional until the M2 benchmark and reference hardware are selected.
- Add no effects beyond the approved five rhythmic operations.
- JUCE 8.0.7 AudioPlayHead::PositionInfo exposes optional PPQ position, last-bar-start PPQ, loop points, looping state, and sample position. It explicitly allows timing fields to be unavailable. Source: https://github.com/juce-framework/JUCE/blob/8.0.7/modules/juce_audio_basics/audio_play_head/juce_AudioPlayHead.h, accessed 2026-10-12.
- Existing CI run 38148457507 at the reviewed commit passed Windows/Linux compile and tests. No REAPER host test, physical audio-device test, latency measurement, 24-hour stress test, or audio-quality listening test was evidenced. These remain open.

## Review verdict

The specification is suitable to guide the next implementation slices after its §5.1 edge/failure contracts and §5.2 measurement protocol are applied. The application is not complete against the P0 specification. Next sequence: PPQ/fallback and safe state fixes → portable scenes/scheduler/history/DSP → wet/dry and trigger/state wiring → product UI → profile/optimize → full QA → merge and final builds.
