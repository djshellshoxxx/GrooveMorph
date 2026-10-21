# F10 Multiband (two-band) processing

Status: IMPLEMENTED (two bands, Classic and Linkwitz-Riley). N-band is out of beta scope (TODO B-06). Updated 2026-10-20 by the A1 completeness audit.
Written 2026-10-20 against `c83e8b2`. Replaces draft `docs/expansion/features/F10-MULTIBAND.md` (PR #6). Shares implementation with
[F03](F03-GROOVE-PROTECTION.md); read that spec for the parameter table, filters and exactness rules.


## Beta audit status (2026-10-20)

- Built: LR2/LR4 topology in `src/core/dsp/Crossover.*`, tested at 44.1/48/96/192 kHz (F10-T03..T06).
- Open: three or more bands (F10-T08, B-06).
- Acceptance rows below carry audited statuses; the full join of ID, test, implementation and UI wiring is in [COMPLETENESS-MATRIX.md](COMPLETENESS-MATRIX.md).

## 1. Objective

Send only part of the spectrum through the rhythm effects: everything above the crossover ("Protect lows") or only the lows ("Lows only").

## 2. As implemented

- Parameters `bandMode`, `bandCrossover`, `bandSlope` (see F03 section 2.3 for ranges and defaults).
- One crossover, one effected band, one bypassed band. The same five effects and the same scene settings apply to the effected band; there are no
  per-band scenes, no per-band effect settings and no per-band placement.
- The engine is fed the effected band; the bypassed band is added back after the engine. If the engine leaves the band unchanged the original input is output (bit-exact).
- All state lives in the processor and is cleared when mode or slope changes; the crossover is recomputed every block (cheap closed forms: one `exp`, one `tan`, one `sqrt`).
  See `process()` and `renderSegment()` in `src/PluginProcessor.cpp`. Mono and stereo are supported (`maxProcessedChannels`).

## 3. State keys

APVTS parameters only.

## 4. Real-time and threading

Same as F03: allocation-free; buffers `bandBypass`, `bandPart`, `dryBuffer` are sized in `prepareToPlay` to the maximum block.
Owner W3 must not allocate when moving the split into core (`src/core/dsp/Crossover.{h,cpp}` suggested name).

## 5. UI wiring

Input tab "Band split" section: "Process" combo (Full band, Protect lows, Lows only), "Crossover" slider (Hz), slope combo. Tooltips explain exactness.

## 6. Acceptance tests

| ID | Check | Status | Location |
|---|---|---|---|
| F10-T01 | Each mode routes the correct band through a closed gate (see F03-T04) | EXISTS | `tests/PluginProcessorTests.cpp` "protect lows: bass survives the closed gate, highs are gated", "lows only: highs bypass the closed gate, bass is gated" |
| F10-T02 | Wet 0 / unchanged band is bit-exact; non-finite input sanitised | EXISTS | PluginProcessorTests "band split at wet 0 preserves finite samples and sanitizes NaN/Inf immediately" |
| F10-T03 | Crossover frequency response (target LR4): low and high each -6 dB at fc, sum flat within 0.1 dB from 20 Hz to 20 kHz at 44.1/48/96/192 kHz | EXISTS | `CrossoverTests.cpp (label F10-T03)` |
| F10-T04 | Stereo channels are filtered independently and identically | EXISTS | `CrossoverTests.cpp (label F10-T04)` |
| F10-T05 | Sample-rate independence (crossover frequency in Hz does not drift between 44.1 and 192 kHz) | EXISTS | `CrossoverTests.cpp` "F10-T03 high -6 dB at fc"; `CrossoverTests.cpp` "F10-T03 flat sum"; `CrossoverTests.cpp` "W3-LR-T02 low rolloff" These checks loop over 44.1, 48, 96 and 192 kHz. |
| F10-T06 | Switching mode/slope/crossover mid-stream produces no click above a stated step threshold | EXISTS | `CrossoverTests.cpp (label F10-T06)` |
| F10-T07 | Latency: the split adds 0 latency (plugin still reports zero) | EXISTS | PluginProcessorTests "zero latency" (re-run after W3) |
| F10-T08 | Three-or-more-band routing | NOT PLANNED for Beta | post-Beta spec |

## 7. Gaps (the Linkwitz-Riley topology was implemented by W3)

- W3: Linkwitz-Riley topology and parameter policy (append a new parameter; do not lengthen `bandSlope`), see F03 section 7.
- Unowned, deferred: more than two bands; per-band effect settings or scenes; per-band placement; visual crossover display.

## 8. Non-goals

Zero-phase or linear-phase splitting (would add latency), spectral/FFT bands, per-band dry/wet.
