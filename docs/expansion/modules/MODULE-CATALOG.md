# Module ownership catalog

Each portable module uses C++20, no JUCE in public types, explicit ownership and deterministic tests. Read-only model and standalone tests link GrooveMorphCore; adapters bind through PluginProcessor on the message thread. Do not create a generic plug-in framework.

| Module | Owns | Dependencies | Runtime thread | Failure contract |
|---|---|---|---|---|
| MusicalTime | host PPQ/sample epoch conversion | existing TransportResolver | audio reads immutable | invalid PPQ -> manual fallback |
| SceneModel | canonical scene and events | existing SceneBank | message publish, audio copy | reject bad revision |
| EventScheduler | bounded event plan dispatch | MusicalTime | audio | late/overflow hold prior plan |
| VariationPlanner | seeded operation/event proposals | SceneModel, constraints | worker | cancelled state immutable |
| MorphEngine | event correspondence plan | SceneModel, timebase | worker | dry-safe unmatched transition |
| ConstraintEngine | manual exclusion masks | SceneModel | worker | reject forbidden mutation |
| EvolutionPlanner | finite stage timeline | variation, history | worker | rollback stage |
| TriggerRouter | priority mapping and queue | MIDIMapper, scheduler | audio+message bridge | overflow counter |
| MIDIMapper | controller profiles and note/CC | stable parameter manifest | message/adapter | capability fallback |
| HistoryManager | undo/branch DAG | SceneBank and StateCodec | message thread | atomic rejected restore |
| StateCodec | migration, size checks | SceneModel | host state callback | preserve previous state |
| AudioImportService | async decode/resampling | asset store | worker only | missing asset signaled |
| MultibandRouter | crossover and band latency | TransformationEngine | audio prepared buffers | bypass aligned |
| GestureTimeline | timestamped action history | TriggerRouter, state | worker + bounded audio | missing ref ignored |
| Host adapters | VST3/Standalone/CLAP capabilities | JUCE + stable API | host appropriate | unsupported capability disabled |

The exact C++ header-level contracts, ABI version and memory budgets must be frozen against resolved MVP integration source and validation. This catalog defines responsibilities, not a claim of completed modules.
