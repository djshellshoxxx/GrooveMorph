# GrooveMorph

This repository is in the M0 engineering spike phase.

The project is intentionally minimal and uses JUCE + CMake to validate the VST3 build flow and the low-latency DSP assumptions before broader implementation begins.

## Quick start

```bash
cmake -B build -S .
cmake --build build
```

## Current milestone

This repo is a minimal JUCE shell for the approved M0 spike. The next step is to validate the plugin loads in REAPER and confirm the bounded rhythmic operations remain low-latency before moving to the core DSP architecture.
