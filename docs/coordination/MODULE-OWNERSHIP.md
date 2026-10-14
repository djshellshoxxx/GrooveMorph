# Module ownership

| Module / file | Owner | Notes |
|---|---|---|
| `src/PluginProcessor.*` | integration owner (single writer) | host adapter, scheduling glue, auto-return, morph, placement slicing |
| `src/PluginEditor.*`, `src/ui/InspectorPanel.*` | integration owner | GUI; inspector tabs are separate classes in one file |
| `src/core/Scheduler.*`, `TransportResolver.*`, `PhraseClock.*` | canonical, unchanged by alpha | extend, never duplicate |
| `src/core/dsp/RhythmEngine.*` | canonical, unchanged by alpha | placement gates its inputs; the chain is untouched |
| `src/core/control/*` | alpha control module | portable, fixed-capacity, no JUCE |
| `src/core/variation/*` | planner (PR #9) + CandidateBuilder | |
| `src/core/scene/*` | scene model/serializer | control state stored under optional keys (format v1 header kept) |
| `docs/control-specs/*`, PR #6 | spec owners | contracts; alpha documents deviations in ALPHA-FEATURE-STATUS.md |
