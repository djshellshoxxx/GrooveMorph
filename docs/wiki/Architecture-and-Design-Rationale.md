# Architecture and Design Rationale

The timing and integration contract is in [docs/TIMING_INTEGRATION_HANDOFF.md](../TIMING_INTEGRATION_HANDOFF.md). This page only summarises the layout.

- **Portable core.** `src/core/` holds phrase timing (`PhraseClock`, `TransportResolver`), the scheduler, the DSP helpers, the control logic, scenes, presets, and trace and visual models. It builds without JUCE.
- **Processor and adapters.** `src/PluginProcessor*.cpp` and `src/PresetManager.cpp` connect the core to JUCE parameters, host transport, MIDI and state.
- **UI.** `src/ui/` holds the editor panels, the Easy view, the Euclid and Print panels, and the Visual window.
- **Callback rules.** Preallocated buffers, cached parameter pointers, bounded handoffs, and defined behaviour for reset, queue overflow, invalid input, latency, bypass and stopped transport.
- **Timing modes.** Automatic, Host PPQ and Sample Clock. The stopped-host rule and the fallback behaviour are in [help/TIMING.md](../../help/TIMING.md).
