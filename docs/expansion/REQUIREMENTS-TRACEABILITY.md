# Expansion requirement traceability — initial mapping

Status labels VERIFIED_COMPLETE, VERIFIED_BLOCKED, NOT_COMPLETE. Current expansion features are NOT_COMPLETE until source, tests, UI wiring and platform evidence exist.

| Feature | Planned modules | Existing/new evidence | Remaining verification |
|---|---|---|---|
| F01 | VariationPlanner + command bridge | New `src/core/variation/VariationPlanner.{h,cpp}`, `tests/core/VariationPlannerTests.cpp` (test added, not executed) | Compile, tests, scene commit/UI, phrase-event generation |
| F02 | MorphEngine + TransitionMixer | Spec supplement only | DSP/UI/automation/host tests |
| F03 | ConstraintEngine + GrooveProtectionEngine | Spec supplement only | Masks + protected audio tests |
| F04 | TriggerRouter + performance UI | Existing scheduler/MIDI note map in MVP | Live triggers, UI, test matrix |
| F05 | EvolutionPlanner | Spec supplement only | Finite planning/stage persistence |
| F06 | IndependentLaneScheduler | Existing host resolver foundation | Checked rational timing and lane tests |
| F07 | EnvelopeDetector + trigger router | Spec supplement only | Sidechain negotiation and trigger tests |
| F08 | HistoryManager | Existing SceneBank/UndoStack | Bounded branch graph and migration |
| F09 | AudioImportService + AudioHistory | Existing bounded RhythmEngine ring history | User assets, capture and IO safety |
| F10 | MultibandRouter | Spec supplement only | Crossover reconstruction/phase and PDC |
| F11 | GestureTimeline | Spec supplement only | Gesture record/edit/replay |
| F12 | MIDIMapper + host adapters | Existing VST3/Standalone and MIDI notes | Full MIDI learn, Windows/Linux/CLAP host tests |

Feature code without reachable controls or validated MIDI/automation path never earns VERIFIED_COMPLETE. Numeric benchmarks, source SHA and CI evidence must be attached per acceptance test; assertions in a source file do not count as test results.
