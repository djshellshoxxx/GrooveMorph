# Fuzz and stress run, 2026-10-21 (Linux x86_64, headless)

Tools added: `tests/FuzzStressTests.cpp` (`GrooveMorphFuzzTests [rounds] [firstSeed] [seconds]`, 3-round smoke in CTest) and
`tests/UiFuzzTests.cpp` (`GrooveMorphUiFuzzTests [rounds] [firstSeed] [actions]`, needs a display, run with `xvfb-run -a`; not a CTest).

What they do: random sample rate (8-192 kHz), block size (1-4096, variable per block), mono/stereo, every host parameter randomised
(including edge values, bypass), random scenes, hostile transport (seek, stop/start, tempo 1-999, odd meters, host without position),
random and corrupt MIDI, UI-thread actions (scene switch/undo/audition/evolution/surprise/gesture/presets/macros/MIDI learn), state
save/corrupt/restore between blocks, hot (>0 dBFS), silent and click input; a second phase imports valid, tiny, 25 s and garbage files,
captures a loop, runs stretch, prints to WAV and checks lengths. The UI tool drives every visible button, slider, combo and text field
in random order (171 distinct controls reached), keys, tabs, resizes and Easy/Full mode switches while audio renders.

Results (all green): ~6.5 M blocks across ~140 audio rounds and ~110 mode rounds in the release build; the same tools under
AddressSanitizer + UBSan (fatal) for ~45 audio rounds, 12+12 mode rounds and 10 UI rounds: no crash, no sanitizer report, no non-finite
output, peak bounded (hot input 4.0 gives at most ~6.6), state round trip always loads. The only harness failure was a too-short wait in
the tool itself (capture starts on the next bar), fixed.

Not measured / open: audio-thread time was sampled but not bounded. On a shared 4-core VM, isolated blocks took 1-26 ms (192 kHz, 32-sample
blocks) in some runs and not on repeat; attributed to scheduler noise, not proven. Real-time deadline measurement on audio hardware stays
under B-01. No DAW, listening or CLAP-host check was done.
