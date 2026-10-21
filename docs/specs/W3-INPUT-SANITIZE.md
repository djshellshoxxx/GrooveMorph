# W3-INPUT-SANITIZE: non-finite samples never reach the output

Status: SPEC (2026-10-20, base `c83e8b2`). Closes KNOWN-ISSUES "a non-finite input sample can still reach the output through the dry path".

## 1. Objective

NaN and +-Inf in the host buffer are replaced by 0 at the very start of `process()` (all channels, before any early return), so the dry path, the bypass ramp, the band split, the onset detector,
loop capture and the history rings only ever see finite audio; and a final guard replaces any non-finite output sample by 0.

## 2. Behaviour

- `core::dsp::sanitizeBlock(float* const* channels, int numChannels, int numSamples) -> int` (header only, noexcept) returns the number of samples replaced; denormals are left alone (the
  processor already runs with `ScopedNoDenormals`).
- Processor: called on the whole buffer at the start of `process()` (also for `processBlockBypassed` and when the engine is not prepared), and once more on the output before returning.
  A counter `nonFiniteSamples` (relaxed atomic, UI/test readable) accumulates input replacements. The per-segment loop in `renderSegment` is removed (redundant).
- Replacement value is exactly 0.0f, so the signal has a click only where the host already delivered garbage.
- Finite inputs are never modified: output is bit-identical to before for all existing tests.

## 3. Tests (`tests/core/InputSanitizerTests.cpp`, `tests/W3ProcessorTests.cpp`)

| ID | Check |
|---|---|
| W3-NF-T01 | `sanitizeBlock` replaces NaN, +Inf, -Inf, counts them, leaves finite values and denormals bit-identical; handles null channels and zero length. |
| W3-NF-T02 | Engine: NaN/Inf in the input with every operation on leaves the history rings finite: a following clean block produces finite output equal to an engine that never saw the garbage after the history window. |
| W3-NF-T03 | Processor: NaN/Inf blocks at wet 0 (dry path), with host bypass, with each band mode and topology (Classic 6, 12, LR2, LR4), before `prepareToPlay` (engine unprepared) and with the transport stopped: every output sample finite; the count matches. |
| W3-NF-T04 | Processor: after a garbage block the next clean block equals the output of a processor that never saw it (no state poisoning). |
| W3-NF-T05 | Output guard: a processor whose engine is fed values near float max does not emit Inf (finite clamp via the guard). |

## 4. Non-goals

Limiting or clipping finite out-of-range samples. Reporting garbage in the UI.
