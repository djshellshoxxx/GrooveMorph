# F03 Groove protection

Status: IMPLEMENTED (downbeat protection, protected regions, Classic and Linkwitz-Riley band split). Beta audit: F03-T06 open (TODO B-09). Updated 2026-10-20 by the A1 completeness audit.
Written 2026-10-20 against `c83e8b2`. Replaces draft `docs/expansion/features/F03-GROOVE-PROTECTION.md` (PR #6).
Cross-refs: [C03](../control-specs/features/C03-CHANGE-PERMISSIONS.md), [F10](F10-MULTIBAND.md).


## Beta audit status (2026-10-20)

- Built: `bandTopology` (Classic / Linkwitz-Riley 12 / Linkwitz-Riley 24) appended after `bandSlope`; the choice list of `bandSlope` was not lengthened (section 7 target met in spirit; `docs/specs/W3-LR-CROSSOVER.md` is the detail).
- Added by the audit: the 4-beat protect span is capped at one bar in 3 and 7 beat bars (F03-T10).
- Open: the Classic 12 dB/oct split is JUCE-side code with no portable test (F03-T06, B-09).
- Acceptance rows below carry audited statuses; the full join of ID, test, implementation and UI wiring is in [COMPLETENESS-MATRIX.md](COMPLETENESS-MATRIX.md).

## 1. Objective

Keep the groove anchors intact while effects play: (a) mute all effects at the start of each bar, (b) mute them inside user-defined
time regions, (c) keep the low end (kick/bass) out of the effects.

## 2. As implemented

### 2.1 Downbeat protection (per scene)
`PlacementConfig::downbeatProtectBeats` (float, 0..4 meter beats, 0 = off; clamped in `sanitize`). In
`evaluate()` (`src/core/control/PlacementEvaluator.cpp`), when > 0, all operations are forced inactive while
`barPhase < min(barTicks, protect*960)`, where `barPhase = positiveMod(globalPos, barTicks)`. It uses the GLOBAL transport position
(`globalBeats`), so a C04 phase shift never moves the protected downbeat. `ticksUntilChange` includes both the end of the protected
span and the next bar start, so the renderer splits segments there (sample accurate for any block size).
`isActive()` treats `downbeatProtectBeats > 0` as active.

### 2.2 Protected regions (per scene, shared with C03)
`PlacementConfig::regions[4]` (`ProtectedRegion{startTicks, lengthTicks}`, `regionCount` 0..4), ticks are 1/960 meter beat on the
pattern-local circular phrase. Inside any region all operations are inactive. Covered by `permissions.placementLocked`.
Sanitised against the phrase length (`start` in `[0,limit)`, `length` in `[1,limit]`).

### 2.3 Low-band protection (global parameters)
Parameters (`PluginProcessor.cpp` `createParameterLayout`):

| ID | Type | Range | Default |
|---|---|---|---|
| `bandMode` | Choice | Full band, Protect lows, Lows only | Full band |
| `bandCrossover` | Float | 40..400 Hz, step 1, skew 0.5 | 120 |
| `bandSlope` | Choice | 6 dB/oct, 12 dB/oct | 6 dB/oct |

In `renderSegment` the input of each segment is split into `low` and `high`; "Protect lows" sends `high` through the engine and
adds `low` back; "Lows only" is the reverse. 6 dB/oct: one-pole low-pass with `coefficient = 1 - exp(-2*pi*fc/fs)`, `high = in - low`.
12 dB/oct: TPT state-variable 2nd-order Butterworth (`k = sqrt(2)`) used for the band sent to the effects, and the bypassed band is
`in - effected` (exact complement). When the engine leaves the band unchanged (`x[i] == part[i]`) the original dry sample is output
(bit-exact). Filter states are cleared when mode or slope changes. Non-finite samples are replaced by 0 before filtering. The audio
thread clamps the crossover to 20..1000 Hz defensively.

## 3. State keys

Scene keys `variation.<i>.placement.protect`, `.placement.regions`, `.region.<r>.start`, `.region.<r>.length` (optional, written only
when non-default). Parameters `bandMode`, `bandCrossover`, `bandSlope` in APVTS state.

## 4. Real-time and threading

Evaluator and band split are allocation-free and lock-free; band state lives in the processor (`bandLowState`, `svfIc1/2`, `bandBypass`,
`bandPart`, `dryBuffer`, all allocated in `prepareToPlay`). Region/downbeat edits are message-thread scene edits published by snapshot.

## 5. UI wiring

Downbeat: Place tab slider "Protect downbeat (beats)". Regions: Limit tab ("Add region", "Remove region", "Region start (beats)",
"Region length (beats)"). Band split: Input tab ("Band split", "Process", "Crossover", slope combo), attached to the three parameters.

## 6. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F03-T01 | Bar-start window silent, later beat active; boundary distances exact | EXISTS | `tests/core/SceneControlTests.cpp` "bar 2 downbeat silent", "beat 2 active", "distance to unprotect" |
| F03-T02 | Protection follows the GLOBAL position under a phase shift; cadence follows global phrase | EXISTS | `tests/core/PhaseResolverTests.cpp` "cadence follows the global phrase"; SceneControl "region mutes", "outside the region is untouched", "region boundary limits the span" |
| F03-T03 | Regions and per-event locks round-trip through the scene format | EXISTS | PhaseResolverTests "phase, regions and locks round-trip", "regions load without a recovery warning" |
| F03-T04 | Protect lows keeps bass through a closed gate; Lows only gates bass; Full band gates both | EXISTS | `tests/PluginProcessorTests.cpp` "full band: gate silences both tones", "protect lows: bass survives the closed gate, highs are gated", "lows only: highs bypass the closed gate, bass is gated" |
| F03-T05 | Wet 0 band split is finite/exact and NaN/Inf are sanitised | EXISTS | PluginProcessorTests "band split at wet 0 preserves finite samples and sanitizes NaN/Inf immediately" |
| F03-T06 | 12 dB/oct slope: effected-band rolloff at 2 octaves | DEFERRED | The Classic 12 dB/oct split lives in PluginProcessor.cpp (JUCE) and was measured offline once (DECISIONS); no portable test exists. Open as TODO B-09. |
| F03-T07 | Linkwitz-Riley crossover (target section 7): low+high magnitude sum flat within 0.1 dB, both bands phase matched, no state discontinuity on parameter sweeps | EXISTS | `CrossoverTests.cpp` "F10-T03 flat sum"; `CrossoverTests.cpp` "F10-T03 low -6 dB at fc"; `CrossoverTests.cpp` "W3-LR-T03 identical input gives identical channels" |
| F03-T08 | Bit-exact dry when the engine leaves a band unchanged, for every slope option | EXISTS | `CrossoverTests.cpp (label F03-T08)` |
| F03-T09 | Crossover or mode changes during playback do not click (peak step bounded) | EXISTS | `CrossoverTests.cpp` "F10-T06 sweep step bounded"; `CrossoverTests.cpp` "W3-LR-T08 no onset click" |
| F03-T10 | Protect span is capped at one bar (`min(barTicks, protect*960)`) in 3/4 and 7/8 with protect = 4 beats | EXISTS | `CompletenessTests.cpp (label F03-T10)` |

## 7. Linkwitz-Riley split (implemented by W3 as `bandTopology`; remaining deferrals listed)

- Owner W3: Linkwitz-Riley split. Target: LR4 (two cascaded Butterworth 2nd-order stages, 24 dB/oct) for low and high, with the
  band routed around the effects passed through the same filter pair so the recombined sum is flat in magnitude (it is an all-pass in
  phase; this is inherent to LR4 and acceptable). The "unchanged band outputs the original dry sample" shortcut stays so the null test
  keeps passing. Because host automation stores normalised choice values, DO NOT lengthen the `bandSlope` choice list; append a NEW
  parameter (for example `bandTopology`, choices "Classic", "Linkwitz-Riley 24") at the end of the layout with version hint 1.
  Old projects lack it and restore to "Classic" (existing behavior).
- Unowned, deferred: transient/onset "detection proposals" and band masks from the draft; per-scene crossover settings.
- Region names and lock groups: deferred (C03 follow-up).

## 8. Non-goals

Automatic detection of kicks, snares or downbeats. Frequency-domain protection. Protecting by band inside events (F10 follow-up).
