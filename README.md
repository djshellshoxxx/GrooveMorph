# GrooveMorph

This repository is in the M0 engineering spike phase.

The project is intentionally minimal and uses JUCE + CMake to validate the VST3 build flow and the low-latency DSP assumptions before broader implementation begins.

Agent workflows are available in [skills/README.md](skills/README.md): full Superpowers, ten DSP/JUCE skills, and token-efficient communication skills without Antigravity protocols. See [AGENTS.md](AGENTS.md) and the [completion-session prompt](docs/COMPLETION_PROMPT.md) for the current branch-integration handoff.

## Quick start

```bash
cmake -B build -S .
cmake --build build
```

## Current milestone

This repo is a minimal JUCE shell for the approved M0 spike. The next step is to validate the plugin loads in REAPER and confirm the bounded rhythmic operations remain low-latency before moving to the core DSP architecture.
