# Audio-trigger material measurements — 2026-10-22

`tests/core/OnsetMaterialTests.cpp` reproducibly renders three synthetic mono
fixtures: a percussive kick/noise loop, sustained 55/110 Hz bass, and a dense
four-tone chord/noise bed. Every fixture lasts eight seconds. No external audio,
random seed or proprietary recording is needed. The renderer seed is fixed.

Measured at 44.1, 48 and 96 kHz, sensitivities 0.2, 0.6 and 1.0, blocks of 1,
64, 480 and 2048 samples. Counts are independent of the tested block size.

| Material | Sensitivity 0.2 | Sensitivity 0.6 | Sensitivity 1.0 |
|---|---:|---:|---:|
| Percussive loop (16 intended hits) | 16 (2/s) | 16 (2/s) | 16 (2/s) |
| Sustained bass | 0 | 0 | 2 (0.25/s) |
| Dense sustained chord/noise | 0 | 1 (0.125/s) | 64 (8/s) |

The same counts were measured at all three sample rates. This reveals a tuning
limitation at maximum sensitivity: sustained harmonic beating can repeatedly
cross the fast/slow envelope ratio, approaching the 120 ms refractory ceiling.
The detector is behaving according to its current specified envelope algorithm;
this is not evidence that such retriggers are musically desirable.

The new test verifies all intended percussion hits, at most one sustained-signal
onset at ordinary sensitivities, block-size invariance and the refractory bound.
It measures maximum-sensitivity sustained behaviour without declaring it acceptable
for musical use: the specification currently defines no false-trigger target.
No DSP threshold or existing acceptance criterion was changed to pass this test.
The initial exploratory <=1-onset assumption at maximum sensitivity failed; it
was not an established product requirement and is recorded here as a finding.

B-07/F07-T09 remains PARTIAL: recorded real material, a product false-trigger
budget, tuning and listening checks remain necessary. These synthetic fixtures
provide reproducible coverage, not a substitute for native listening/host QA.

Validation: direct GCC 13.3.0 C++20 Release compilation and execution passed.
The existing portable core suite passed 56/56 before adding this separate target.
