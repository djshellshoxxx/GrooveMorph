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

## Addendum: CLAP validator (clap-validator 0.4.1, Linux, run locally)

The published v0.2.0-beta.1 notes list clap-validator FAILED on all four platforms (informational). Reproduced on Linux: 44 run, 30 passed, 4 failed, 1 warning.
- `param-conversions` (fixed here): the skewed "Slice trim" parameters printed seven digits and did not survive text -> value -> text.
  They now print three decimals. Re-run: 31 passed, 3 failed.
- `state-reproducibility-basic/binary/buffered` (open): the validator reports ~39 parameters differing after a state reload. The plugin side
  was traced with debug output: the saved state holds the validator's values and `replaceState` restores them (Chaos 0.0556 saved, 0.0 on the
  fresh instance, 0.0556 after load). `GrooveMorphFuzzTests` now also checks, on a non-message thread and the way the CLAP wrapper writes
  parameters (`setValue` + listener notify), that processBlock does not rewrite host-set parameters and that a fresh instance holds every
  value straight after `setStateInformation`; both pass. The cause is therefore not found; the likely place is the clap-juce-extensions
  adapter or the validator's read timing. Needs a CLAP host or the adapter's source traced. Not claimed fixed.
- `state-invalid-random` (warning): random bytes load "successfully" (the processor keeps the previous state and returns without error);
  harmless, but the CLAP adapter cannot report failure because JUCE's `setStateInformation` is void.
