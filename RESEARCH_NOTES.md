# GrooveMorph research and implementation status

Research/specification details: docs/SPECIFICATION.md. Requirement coverage and evidence: docs/TRACEABILITY.md. Full spec audit: docs/SPEC_REVIEW.md.

## Approved direction

- C++20, CMake, JUCE 8.0.7.
- Windows x64 and Linux x86_64; VST3 and JUCE Standalone.
- REAPER is the reference DAW for host validation.
- CLAP, full phrase capture, pitch-preserving time stretching, multiband processing, generative variation, cloud services, audio-file import, and broad unrelated effect suite are deferred.
- MVP is a phrase/variation workflow with bounded rhythmic transformations and explicit transition behavior.

## Active implementation handoff

The active source line is claude/awesome-keller-jx2ck0 at dfcabda030f7e1432f4b51baad471b26ddc91f0c. The continuation branch codex/mvp-continuation starts from this exact commit. It preserves the earlier codex/m0-validation history; no M0 work is being regenerated.

Current code includes PhraseClock, TransportResolver, APVTS scalar timing/intensity parameters, basic timing status UI, shared operation settings, and headless processor tests. The processor remains a timing/gain spike. It does not yet implement transformations, scenes, transitions, MIDI scene triggers, scene state, undo/audition, or the MVP editor.

## Build evidence at handoff

GitHub Actions workflow run 38148457507 for exact commit dfcabda030f7e1432f4b51baad471b26ddc91f0c completed successfully across Windows and Linux portable-core and JUCE plugin/Standalone jobs. Portable timing/state tests passed within CI. This is build/test evidence only; it does not verify REAPER, physical audio devices, plugin automation/latency behavior, or audio quality.

## Current gates

| Gate | Status | Evidence / next action |
|---|---|---|
| Portable core builds/tests | Passed on Windows and Linux CI | Run 38148457507 |
| JUCE VST3/Standalone builds | Passed on Windows and Linux CI | Run 38148457507 |
| Windows Standalone launch/audio device | Not yet tested | Add artifact + launch smoke step, then test with physical device if available |
| Linux Standalone launch/audio device | Not yet tested | Requires desktop runtime/device environment |
| REAPER VST3 load/state/automation/transport | Not yet tested | Requires REAPER on each target OS |
| Realtime allocation/lock audit | Static review only | Cache APVTS pointers; trace JUCE calls; instrument callback |
| Product audio quality/latency/performance | Not yet measurable | Implement wet path and DSP first, then benchmark |

## Required sequence

Follow the current user direction: (1) complete spec review and update traceability; (2) bug search and fix confirmed defects; (3) profile and optimize measured hotspots; (4) QA on the integrated implementation and fix findings; (5) merge reviewed work and create final build artifacts. Do not claim full completion while host/audio-device gates remain unavailable.
