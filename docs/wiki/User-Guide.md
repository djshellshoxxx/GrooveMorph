# User Guide

The full illustrated guide is in the repo under `help/` (start with `help/QUICK-START.md`, then `help/CONTROLS.md`). This page is the short version.

## First run

1. Insert GrooveMorph as an audio effect on a rhythmic track (a drum loop works well).
2. Play the host transport. Keep **Bypass** off.
3. Set **Intensity** and **Dry / wet** above zero. If you only hear dry audio, check the selected variation and the operations.
4. Leave timing on **Automatic**. It uses host musical time (PPQ) when the host provides it, and falls back to the sample clock otherwise.

## Timing

- **Automatic**: host PPQ if valid, otherwise sample clock.
- **Manual**: set **Manual tempo (BPM)**, **Manual beats per bar**, **Manual beat unit** and **Phrase length (bars)**. Use this when the host gives no musical time.
- The line under the timing controls says whether timing comes from the host, manual settings or free run.

Phrase length sets how many bars make one phrase. Variations switch and transitions land on the bar line. See `help/TIMING.md`.

## Variations

Four variations, A to D. Each one holds its own set of operation settings. You can edit one, duplicate scenes, rename them, undo and redo, and audition a change before you commit to it.

- **Generator**: pick a seed and a style, get a gallery of three options, and keep the one you like. Locks protect fields, events or placements you want to keep.
- **Transition**: choose how a switch starts (follow the phrase, restart at launch or snap to the next meter beat), with an offset of -4 to +4 beats.
- **Automatic return**: after 1 to 32 bars or phrases, return to the previous variation or to A to D on the bar line.

## The five operations

| Operation | What it does |
|---|---|
| Repeat | repeats a short slice |
| Reverse slice | plays a bounded slice backwards |
| Gate | rhythmic volume gate |
| Half-time | half-speed playback feel |
| Tape stop | a tape-stop style slowdown curve |

Each operation has **Enable**, **Depth** (0 to 1) and **Slice** length (1/16 to 4 beats).

## Controls you can automate

- **Intensity** and **Dry / wet** (master)
- **Variation** (switches A to D)
- **Morph** between scenes, with a target and an amount
- MIDI CC macros in the Perform tab

## Protect the groove

- **Downbeat protect** silences effects for 0 to 4 beats each bar.
- **Protect lows** splits the band at 40 to 400 Hz (6 or 12 dB per octave) so the low end is left alone.
- Per-effect locks and up to four protected regions stop changes you do not want.

## MIDI

Right-click a control for MIDI learn. The MIDI note map and CC macros are saved with the project. `help/RIGHT-CLICK-AND-MIDI.md` covers the details.

## Troubleshooting

See `help/TROUBLESHOOTING.md`. Quick checks:

- Only dry audio: check Intensity, Dry / wet, the selected variation and whether any operation is enabled.
- Timing wrong: switch from Automatic to Manual and enter the tempo and meter.
- Known defects: `docs/alpha/KNOWN-ISSUES.md`.

## Limits

- No DAW or hardware testing has been done yet.
- Full phrase capture and pitch-preserving time stretch are deferred.
- CLAP is deferred.
