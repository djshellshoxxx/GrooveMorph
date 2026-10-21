# Requirement traceability

This table supersedes the M0-only checklist. It describes the reconciled processor/core and the integration work dated 2026-10-12. Source presence does not establish host compatibility or release readiness. See [the integration audit](INTEGRATION_AUDIT.md) for discrepancies and verification limits.

| Requirement | Implemented behavior and modules | Verification evidence | Remaining check or discrepancy |
|---|---|---|---|
| GM-FR-001 phrase grid | `PhraseClock`, `TransportResolver`: tempo, meter, phrase bars, signed positions and PPQ/bar-boundary calculations | `PhraseClockTests`, `TransportResolverTests` | Explicit editable phrase origin/subdivision and event lanes are absent; UI completion tracks existing grid controls |
| GM-FR-002 four variations | `SceneBank` owns four independent named sets of operation settings; processor snapshots validated edits | `SceneBankTests`, processor scene round trip | Scene data is uniform per-operation settings, not arbitrary phrase event lanes |
| GM-FR-003 rhythmic operations | `RhythmEngine`: repeat, previous-slice reverse, gate, half-speed and tape-stop rate curve; fixed operation order and bounded history | `DspRhythmEngineTests`: dry null, stereo equality, repeat/reverse/gate fixtures, finite output, history bounds, discontinuity ramps | Half-time/tape-stop auditory behavior, drum fixtures and target-host listening remain pending |
| GM-FR-004 scheduled changes | `Scheduler` handles immediate, beat, bar, phrase and manual transitions; processor splits at MIDI/switch offsets | `SchedulerTests`; headless processor timestamp/countdown tests | Target-host tempo ramp, loop and automation timing checks |
| GM-FR-005 intensity/depth | Intensity and dry/wet combine into wet amount; per-operation enable/depth/slice are scene state; ramps in engine | Core and headless dry-null and transformation tests | Auditory automation sweep and accepted click threshold on target hardware |
| GM-FR-006 timing fallback | Host fields are independently validated; manual tempo/meter fill missing values; PPQ and sample calculations are preserved | `TransportResolverTests`; headless missing/invalid-host tests | Automatic/Host PPQ/Sample Clock selector and persisted parameter are integrated; explicit Sample Clock free-runs without host play state; target-host tests remain |
| GM-FR-007 automation | Stable APVTS IDs for intensity, dry/wet, tempo, meter, phrase bars, active variation, transition mode, timing mode and bypass | Headless parameter state and variation scheduling tests | Target DAW automation/save/reopen; operation contents are scene state, not host automation parameters |
| GM-FR-007b operation automation (beta A06) | `core/control/OperationTrims`: appended APVTS ids `op<N>Enable/Depth/Slice` per operation, applied as an overlay in `effectiveSettings()`; stored scenes untouched | `OperationTrimsTests` (ids, identity, clamps); processor tests A06-06..08 (CI only) | Native host automation/save/reopen; spec `docs/specs/W10-OP-TRIMS.md` |
| GM-FR-008b MIDI assignment (beta A04) | `MidiNoteMap::planAssign/assign` (swap), `releaseAll` on CC 120/123, `CcInbox` travel counter and `pickLearnCc`, `MidiLinkTable::linkChecked` | `MidiNoteAssignTests`, `MidiLinksTests`; processor tests A04-08/09, PANIC-1 (CI only) | Swap dialog and table-full message are UI code; spec `docs/specs/W10-MIDI-ASSIGN.md`, `W10-MIDI-PANIC-RATES.md` |
| GM-FR-008 MIDI | Configurable four-note map, timestamped note-on switches, default ignored note-off and optional return-to-previous | `SceneBankTests`; headless MIDI timestamp/PPQ-split tests | Routing in target hosts; one held-note state defines latest-note priority rather than a polyphonic stack |
| GM-FR-009 undo/audition | `UndoStack`, `AuditionSession`, processor scene APIs; preview does not mutate stored bank; compare/commit/revert | `SceneUndoAuditionTests`; headless processor audition and undo tests | Editor preview/edit/revert/close regression passes; interactive keyboard/accessibility QA remains |
| GM-FR-010 state recall | APVTS host state embeds versioned scene text, names, settings, seed, note map and active variation; malformed scene recovers with report | `SceneSerializerTests`; headless round trip, malformed/oversized state tests | Actual DAW project reopen and prior-version upgrade fixtures |
| GM-FR-011 status | Processor atomically exposes active/queued scene, countdown, switch time, timing source/missing fields, playhead and MIDI events | Headless processor status assertions | Editor shows active/queued/countdown/phrase/missing fields; MIDI event absence indicates no notes received, not proven routing unavailability |
| GM-FR-012 bypass | Host/internal bypass ramps to exact dry; intensity/dry-wet ramp within DSP; zero reported algorithmic latency | Headless bypass ramp/dry assertions; core null test | DAW PDC/bypass/mono-stereo tests |
| GM-QR-001 real-time safety | History allocates in prepare; process uses fixed arrays, atomic status, scene try-lock and bounded DSP loops; host seeks invalidate history in O(1) | Static callback review; portable deterministic fixtures | Actual allocation/lock instrumentation, thread stress and callback deadline measurements |
| GM-QR-003 bounded memory | Four history rings/channel; read windows clamp to four seconds of storage | Core clamp/finite fixtures | Slice tooltips disclose operation-specific four-second history limits; target memory/CPU profile |
| GM-QR-004 click-free updates | Operation/read-position crossfades, gate ramps, intensity smoothing and bypass fade | Core sine toggle bounds, headless bypass bounds | Accepted threshold and listening tests; seek content changes can still be audible |
| GM-QR-005 timing | Wide signed sample math and fresh PPQ boundary calculations; scheduler re-evaluates each segment | Clock/resolver/scheduler fixtures and PPQ MIDI split regression | Target-host sample offset assertions |
| GM-QR-006 latency | Causal look-behind operations; processor reports zero latency | Headless zero-latency assertion | Host PDC/impulse validation |
| GM-QR-007 deterministic output | Reset clears DSP state; same scene/input yields identical output; seed persists without random generation | Core deterministic-reset test; serializer seed round trip | Offline/real-time host render comparison |
| GM-QR-011 portability | Standard-library portable core, pinned JUCE 8.0.7 VST3/Standalone build definition | Local GCC 13.3 Release: all seven portable CTest targets passed | Linux VST3 + Standalone and 8/8 CTest pass on combined code; Windows/Linux builds and processor tests, sanitizer tests and Windows startup passed at 7f2784a; see TIMING_INTEGRATION_HANDOFF.md |
| N06 CLAP + beta packaging | CLAP via clap-juce-extensions (pinned commit), single-source `VERSION`, `beta-release.yml`, `tools/package/` | `VersionTests` (local, core); CLAP build, ctest per platform, clap-validator verdict and packaging only run in CI | CI run of `beta-release.yml` (see docs/beta/RELEASE-PROCESS.md); CLAP host load, install on clean machines, DAW and listening tests not done |

## Current verification and next actions (2026-10-13)

The combined runtime built as Linux VST3/Standalone; all eight matching Linux test
executables (seven core plus processor/editor) passed again on 2026-10-13.
Windows/Linux CI, ASan/UBSan and Windows startup passed at 7f2784a; exact links are
in TIMING_INTEGRATION_HANDOFF.md. Runtime source/tests/workflows match main 61d5156.
No new DAW/device/performance/installation results are claimed.

Current feature gaps and reproduced scene-host/MIDI workflow defects are tracked
in [the feature audit](feature-audit/FEATURE_AUDIT_2026-10-13.md) and [TODO](TODO.md).
The earlier stale-history regression was observed failing with reset disabled and
passing after restoration; that is historical regression evidence, not a new run.

## Deferred scope

GM-FR-013–018 cover transient analysis, low-frequency protection, controlled generation, external bank files, sidechain triggering and full phrase capture. They are intentionally outside this timing reconciliation. The separate F01–F12 expansion specification branch has not been folded into this integration or declared implemented.

## Visual feedback window (2026-10-14)

Specification: [docs/visual-feedback/SPEC.md](visual-feedback/SPEC.md); plan: [INTEGRATION-PLAN.md](visual-feedback/INTEGRATION-PLAN.md).
All evidence below is automated Linux tests plus CI; nothing here was validated in a DAW, on audio hardware, or by listening.

| Requirement | Implemented behavior and modules | Verification evidence | Remaining check or discrepancy |
|---|---|---|---|
| VF-01 audio unchanged by tracing | `RhythmEngine::setTraceSink` is read-only; `TraceRing` | `TraceEngineTests` (bit-identical output with null/inactive/active sink at block sizes 1/7/64/128/513), `EngineGoldenTests`, `PluginProcessorTests` | Real-host confirmation |
| VF-02 bounded callback cost | One 96-byte frame per 128 samples, no allocation/lock/I/O, ring allocated in constructor | Offline benchmark: overhead below noise (0.88 % vs 0.81 % of real time, 60 s stereo, all five effects) | No real-time deadline measurement in a host; no allocation-instrumented run |
| VF-03 portable core | `src/core/trace`, `src/core/viz` (no JUCE); JUCE only in `src/ui/visual` | Core-only build and tests; ASan/UBSan clean | CI include audit not automated |
| VF-04 no visual state in project state | View prefs stored by the window only | State round-trip tests unchanged | - |
| VF-05 closed window means tracing off | `VisualContent::setActive` | `PluginProcessorTests` (frames stop when closed) | - |
| VF-06 accessibility | Ledger text equivalent, accessible names, Reduce motion, Esc/C keys, palette checks | `PaletteTests` (WCAG contrast, Machado-2009 simulation), `PluginProcessorTests` | Screen-reader pass and human colour-blind review not done |
| VF-07 no Gross Beat likeness | Thread weave, rings, stations, orbit; no XY envelope grid, no sweep line, no clock, different palette | Design review rules in PLAN 4b | User side-by-side review pending |
| VF-08 planned vs measured consistent | `StageGeometry` shared by engine and planner; `PlanBuilder` mirrors processor gating | `PlannedPathTests` and `PlanBuilderTests`: planner vs real-engine 96-100 % within 0.01 beat; misses at slice boundaries | Evolution, gestures, audio triggers not modelled (marked as limits) |
| VF-09 honest idle/invalid/preview | Ledger banners, stations "stopped/held off", orbit preview note | `LedgerTests`, `StageModelTests` | - |
| N02 preset browser | `src/core/preset/` (`Preset`, `FactoryPresets`, `PresetLibrary`), `PresetManager`, processor `applyPreset`/host programs, `ui/PresetBar`; spec `docs/specs/N02-PRESETS.md` | `PresetTests` (N02-T01..T15: factory validity, round trip, hostile/fuzz input, library rules, undo step) | Host program mapping, file I/O and PresetBar are not exercised by automated tests; native build and REAPER check pending |
| N01 Filter Sweep / Bit Crush | `docs/specs/N01-EFFECTS-SWEEP-CRUSH.md`; `core/dsp/SweepCrush.h`, `RhythmEngine`, `SceneSerializer` (optional keys), UI rows | `EffectsSweepCrushTests` (N01-T01..T08, T12), `SceneSerializerTests` (T09 incl. legacy fixture), `VariationPlannerTests` (T10), `PaletteTests` (T11) | UI not compiled locally; no listening test |
| N03 Euclid patterns and event chance | `EuclidPattern` (Bjorklund, apply/clear candidates through `enforcePermissions`), `Chance.h` (stateless seeded hash), `PlacementEvaluator` chance gate, optional scene keys `event.K.chance`, `lane.chance.OP`, `lane.seed`, `EuclidPanel` in the Place tab | `EuclidChanceTests` N03-T01..T20 (known rhythms, evenness, rotation, capacity, locks, regions, legacy load, block-split invariance, 20000-cycle statistics, planned path) | EuclidPanel/Place-tab hook not compiled locally; manual UI pass pending; see docs/specs/N03-EUCLID-CHANCE.md |
| N05 Easy mode / Chaos / Surprise me / guide | `docs/specs/N05-EASY-MODE.md`; `core/control/ChaosMapper`, `core/variation/Surprise`, `core/easy/*` (portable); `ui/EasyView`, `ui/FirstRunGuide`, `PluginProcessorEasy.cpp` (JUCE); `chaos` APVTS parameter and `Ui` state child | `ChaosMapperTests` (N05-C1 to C6), `SurpriseTests` (S1, S2), `EasyViewTests` (E2 to E4), `PluginProcessorTests` N05 block (C7, C8, S3, E1, panic, legacy state) | UI behavior (N05-U1) has no automated test; no screenshots or listening checks; processor tests need a JUCE build |
