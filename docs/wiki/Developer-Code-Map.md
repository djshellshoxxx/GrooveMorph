# Developer Code Map

Paths are relative to the repo root.

## Directory tree

```
GrooveMorph/
├── CMakeLists.txt                  project 0.1.0; GrooveMorphCore static lib (src/core/*.cpp, globbed);
│                                   GROOVEMORPH_CORE_ONLY_BUILD option; JUCE 8.0.7 fetch; juce_add_plugin GrooveMorphPlugin
├── src/
│   ├── PluginProcessor.h/.cpp      GrooveMorphAudioProcessor: host parameters (stable IDs), state, processBlock
│   ├── PluginEditor.h/.cpp         JUCE editor
│   ├── core/                       portable C++20 core, no JUCE
│   │   ├── OperationParams.h       OperationType (Repeat, ReverseSlice, Gate, HalfTime, TapeStop), OperationSettings
│   │   ├── PhraseClock.h/.cpp      PhraseGrid, PhraseClock
│   │   ├── TransportResolver.h/.cpp  host PPQ, manual or sample-clock timing
│   │   ├── Scheduler.h/.cpp        SwitchEvent, Scheduler (bar-line variation switches)
│   │   ├── MidiLinks.h/.cpp        user MIDI CC links (MIDI learn), portable
│   │   ├── dsp/                    RhythmEngine (RhythmTiming, voices, taps), LoopSource, LoopImport,
│   │   │                           OnsetDetector, DemoSource, StageGeometry
│   │   ├── scene/                  SceneBank (Variation, SceneSnapshot), SceneSerializer, UndoStack,
│   │   │                           AuditionSession, MidiNoteMap
│   │   ├── control/                SceneControl (protected regions, permissions, phase, activity),
│   │   │                           PlacementEvaluator, GestureLane, VariationBlend, AutoReturn, PhaseResolver
│   │   ├── variation/              VariationPlanner, CandidateBuilder (seeded generator)
│   │   ├── viz/                    Visual window model: Ledger, WeaveLayout, StageModel, PlanBuilder,
│   │   │                           PlannedPath, PathResolver, LapRecorder, Palette
│   │   └── trace/                  TraceRing (atomic ring of frames), TraceFrame
│   └── ui/
│       ├── ControlMenu.h/.cpp      right-click menu
│       ├── InspectorPanel.h/.cpp   inspector panel
│       └── visual/                 VisualWindow, RingsView, StationsView, WeaveView, LedgerView, Glyphs, VisualStyle
├── tests/
│   ├── core/*Tests.cpp             29 standalone CTest executables (one per file), e.g. SchedulerTests, EngineGoldenTests, SceneSerializerTests
│   └── PluginProcessorTests.cpp    JUCE plug-in tests (built when JUCE is on)
├── tools/                          EditorSnapshot.cpp (renders editor snapshots), other helpers
├── help/                           illustrated user guide (QUICK-START, CONTROLS, TIMING, WORKFLOWS, TROUBLESHOOTING, ...)
├── docs/                           SPECIFICATION, TRACEABILITY, TODO, alpha/, qa/, control-specs/, integration/, visual-feedback/
├── .github/workflows/              build.yml (core and JUCE builds), windows-alpha.yml, windows-standalone-smoke.yml,
│                                   macos-alpha.yml, pluginval.yml, portable-core-sanitizers.yml
└── shared/, skills/                shared tooling and agent skills (not app code)
```

## Key symbols

| Symbol (file) | Purpose |
|---|---|
| `GrooveMorphAudioProcessor` (`src/PluginProcessor.h:44`) | JUCE processor. Stable IDs: `masterIntensity`, `dryWet`, `manualTempo`, `manualMeterNumerator/Denominator`, `phraseBars`, `activeVariation`, `transitionMode`, `timingMode` (`PluginProcessor.h:49` onward) |
| `OperationType`, `operationCount`, `variationCount = 4`, `OperationSettings` (`src/core/OperationParams.h`) | the five operations, 4 variations, per-operation settings |
| `RhythmTiming` (`src/core/dsp/RhythmEngine.h:15`) | samples per beat, beat at block start, playing flag, per-operation phase offsets |
| `RhythmEngine` (`src/core/dsp/RhythmEngine.h:52`) | renders the operations per voice |
| `Scheduler` (`src/core/Scheduler.h:38`), `SwitchEvent` (`:22`) | bar-line switching |
| `PhraseClock`, `PhraseGrid` (`src/core/PhraseClock.h:12`, `:15`, `:25`) | phrase timing |
| `TransportResolver` (`src/core/TransportResolver.h`) | picks host PPQ, manual or sample timing |
| `Variation`, `SceneSnapshot`, `SceneBank` (`src/core/scene/SceneBank.h:31`, `:43`, `:52`) | scene data and bank |
| `SceneSerializer`, `UndoStack`, `AuditionSession`, `MidiNoteMap` (`src/core/scene/`) | state I/O, undo, preview, note mapping |
| `SceneControl` (`src/core/control/SceneControl.h:104`) with `ProtectedRegion`, `PlacementConfig`, `LaneEvent`, `EventLane`, `PermissionConfig`, `ActivityConfig`, `PhaseConfig` | controlled change rules |
| `VariationPlanner`, `CandidateBuilder` (`src/core/variation/`) | seeded variation generation |
| `TraceRing` (`src/core/trace/TraceRing.h:16`) | lock-free frame ring for the UI |

## Where do I find X / to change X, edit Y

| I want to... | Edit |
|---|---|
| add a rhythmic operation | `OperationType` in `src/core/OperationParams.h`, its render in `src/core/dsp/RhythmEngine.cpp`, its UI in `src/ui/` or `PluginEditor.cpp`, then a test in `tests/core/DspRhythmEngineTests.cpp` |
| change how a switch lands on the bar | `src/core/Scheduler.cpp`, `src/core/PhraseClock.cpp` |
| change host timing handling | `src/core/TransportResolver.cpp` |
| add a host parameter | `src/PluginProcessor.h` (add an ID; never repurpose one) and its APVTS setup in `src/PluginProcessor.cpp` |
| change the scene save format | `src/core/scene/SceneSerializer.cpp` and `tests/core/SceneSerializerTests.cpp` |
| change undo behaviour | `src/core/scene/UndoStack.cpp` |
| change what generation may change | `src/core/control/SceneControl.cpp`, `PlacementEvaluator.cpp`, `GestureLane.cpp` |
| change the generator | `src/core/variation/CandidateBuilder.cpp`, `VariationPlanner.cpp` |
| change auto-return or transitions | `src/core/control/AutoReturn.cpp`, `PhaseResolver.cpp` |
| change MIDI links or note map | `src/core/MidiLinks.cpp`, `src/core/scene/MidiNoteMap.cpp` |
| change the Visual window | `src/core/viz/` (model) and `src/ui/visual/` (drawing) |
| change the loop import or onset detection | `src/core/dsp/LoopImport.cpp`, `OnsetDetector.cpp` |

## Gotchas for navigation

- `src/core/` must stay free of JUCE. Put any JUCE type in `src/PluginProcessor.*`, `PluginEditor.*` or `src/ui/`.
- The core library globs `src/core/*.cpp` at configure time. A new file under `src/core/` needs a CMake re-run.
- Test files are one executable each, found by the glob `tests/core/*Tests.cpp`.
