# GrooveMorph user help

> Documentation status: `CONTROLS`, `QUICK-START`, `WORKFLOWS`, `TIMING` and the SVG images describe the original MVP editor (commit `7f2784a`, PR #7). The alpha adds inspector tabs (Place, Gen, Scene, Morph, Limit, Input, Perform, Info), the Visual window, transport buttons and right-click menus; only `RIGHT-CLICK-AND-MIDI` and `VISUAL-WINDOW` cover the newer controls, and the tabs are described in `docs/alpha/USER-TEST-GUIDE.md` and the in-app tooltips. This is a user guide, not evidence of DAW qualification.

## Start here

1. [Quick start](QUICK-START.md): getting audio through the effect and changing variations.
2. [Interface and controls](CONTROLS.md): every visible control, with linked annotated visual references.
3. [Workflow recipes](WORKFLOWS.md): quantized scene changes, editing, MIDI, preview/compare, and standalone timing.
4. [Timing and transitions](TIMING.md): Automatic vs Host PPQ vs Sample Clock; queue semantics and fallback.
5. [Troubleshooting](TROUBLESHOOTING.md): silent output, missing timing, held transitions, MIDI, host state.
6. [Visual guide and screenshot capture](VISUAL-GUIDE.md): arrows, numbered labels, and screenshot verification process.
7. [The Visual window](VISUAL-WINDOW.md): the live picture of what the plugin is doing to the sound (alpha).
8. [Right-click menu, MIDI links, Play/Stop and Reset](RIGHT-CLICK-AND-MIDI.md): exact values, copy/paste, MIDI learn, the transport buttons, loop tempo.
9. [Seeded generator and candidate gallery](GENERATOR.md): repeatable options, event-pattern generation, preview/apply and saved local history.
10. [MIDI notes, MIDI learn and host automation of effects](MIDI-AND-AUTOMATION.md): note swap, learn rules, panic messages, per-effect automation trims, update rates.
11. [Keyboard editing, event lanes and screen readers](KEYBOARD-AND-ACCESSIBILITY.md): create events with the keyboard, thin events, phrase-end badge, undo steps, accessible names.

## Scope

GrooveMorph is a **rhythmic audio effect** (not an instrument). This version implements four variations A–D, five short-window transformations, wet/dry mixing, master effect intensity (zero = dry), transition quantization, MIDI note triggering, variation audition, undo/redo, host parameter/state persistence, and host/manual timing.

The illustrated SVGs in `help/images/` are **source-derived UI diagrams**, **not captured screenshots**. They map to bounds and labels in `src/PluginEditor.cpp` and deliberately distinguish verified controls from imagery. True Windows/Linux screenshots, arrow overlays and visual QA must be captured from a running built binary before calling the image set complete. See [visual guide](VISUAL-GUIDE.md).

## What this beta does not claim

The original MVP had no event lanes, file import, morphing or band split; the alpha adds them (see `docs/alpha/ALPHA-FEATURE-STATUS.md`). Full-song capture, time stretching, CLAP export and validated native DAW compatibility are not established. The F01–F12 specifications are drafts (PR #6).

## Useful code links

- [Editor controls and interactions](../src/PluginEditor.cpp)
- [Processor and parameter implementation](../src/PluginProcessor.cpp)
- [Timing integration contract](../docs/TIMING_INTEGRATION_HANDOFF.md)
- [Build/setup instructions](../GETTING_STARTED.md)
