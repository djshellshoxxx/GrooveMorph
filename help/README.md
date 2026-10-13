# GrooveMorph user help

> Documentation target: integrated MVP editor at commit `7f2784a0571f2a6ffb93ea261c386cd89cdde6af` (PR #7). The UI may change in later branches. This is a beta user guide, not evidence of final DAW qualification.

## Start here

1. [Quick start](QUICK-START.md): getting audio through the effect and changing variations.
2. [Interface and controls](CONTROLS.md): every visible control, with linked annotated visual references.
3. [Workflow recipes](WORKFLOWS.md): quantized scene changes, editing, MIDI, preview/compare, and standalone timing.
4. [Timing and transitions](TIMING.md): Automatic vs Host PPQ vs Sample Clock; queue semantics and fallback.
5. [Troubleshooting](TROUBLESHOOTING.md): silent output, missing timing, held transitions, MIDI, host state.
6. [Visual guide and screenshot capture](VISUAL-GUIDE.md): arrows, numbered labels, and screenshot verification process.

## Scope

GrooveMorph is a **rhythmic audio effect** (not an instrument). This version implements four variations A–D, five short-window transformations, wet/dry mixing, master effect intensity (zero = dry), transition quantization, MIDI note triggering, variation audition, undo/redo, host parameter/state persistence, and host/manual timing.

The illustrated SVGs in `help/images/` are **source-derived UI diagrams**, **not captured screenshots**. They map to bounds and labels in `src/PluginEditor.cpp` and deliberately distinguish verified controls from imagery. True Windows/Linux screenshots, arrow overlays and visual QA must be captured from a running built binary before calling the image set complete. See [visual guide](VISUAL-GUIDE.md).

## What this beta does not claim

No arbitrary step/event-lane editor, editable phrase origin or beat subdivision, full-song buffer capture, audio-file import, scene morphing, multiband engine, CLAP export, or validated native DAW compatibility are established by this MVP. The expansion F01–F12 specifications live separately and are drafts.

## Useful code links

- [Editor controls and interactions](../src/PluginEditor.cpp)
- [Processor and parameter implementation](../src/PluginProcessor.cpp)
- [Timing integration contract](../docs/TIMING_INTEGRATION_HANDOFF.md)
- [Build/setup instructions](../GETTING_STARTED.md)
