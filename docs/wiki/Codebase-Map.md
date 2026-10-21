# Codebase Map

| Path | Contents |
|---|---|
| `src/core/` | portable domain code: `dsp`, `control`, `scene`, `preset`, `easy`, `variation`, `viz`, `trace`, plus `PhraseClock`, `Scheduler`, `TransportResolver`, `MidiLinks` |
| `src/PluginProcessor*.cpp`, `src/PluginEditor.*` | JUCE processor and editor |
| `src/ui/` | editor panels: Easy view, Euclid, Print, Inspector, presets bar, first-run guide, waveform, Visual window |
| `src/PresetManager.*`, `src/ServiceTimer.h` | preset management and service timer |
| `tests/core/` | portable core test sources (56 files) |
| `tests/` | JUCE-side processor and audio smoke tests |
| `tools/` | packaging scripts and parameter-table generator |
| `help/` | user help and visual guides |
| `docs/` | specifications, alpha and beta notes, QA, coordination, TODO and traceability |
| `skills/` | agent skill bundle (see skills/README.md) |
