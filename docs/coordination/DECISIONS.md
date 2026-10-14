# Decisions

- **Scene control state lives in `VariationSettings::control`** (placement, lane, permissions, activity). Trivially copyable, so the existing SceneSnapshot handoff to the audio thread is unchanged.
- **Scene file format stays `v1`.** Control keys are written only when non-default and ignored by older builds; old files load with inert defaults. No migration step is needed.
- **Placement gates operation `enabled`/`depth` per slice; the RhythmEngine is unmodified.** The renderer splits at the next placement boundary (nearest sample, >= 1 sample). Consequence: event edges are sample-accurate but each operation keeps its shared beat phase (the spec's LegacyUniform shape); per-event phase restart is not implemented.
- **Ticks are 1/960 of a meter beat, not of a quarter note** (spec uses quarter ticks). Equivalent for x/4 meters; simpler and exact for every supported subdivision. Revisit if per-event anchors are added.
- **Host Variation parameter = requested variation.** The message thread realigns it to the sounding variation when nothing is pending and the audio thread has read the current value; echo writes are ignored by the audio thread. Never written from the audio thread.
- **Locks are enforced after generation** so random draws never depend on locks (spec C03).
- **Generator previews through the existing AuditionSession**, sourced from the stored scene, so nothing is committed until Apply.
- **Morph/auto-return are APVTS parameters** (IDs appended, never reused); locks, placement, events are scene state.
- **Auto-return requests a NextBar switch one bar early** so the existing Scheduler lands it on the bar line.
- **Demo loop is not a parameter and not saved.** It overwrites the input in the callback while the transport is playing.
- **Per-event cycle restart is an engine phase offset** (`RhythmTiming::phaseOffsetBeats`, zeros = legacy grid, bit-identical). The evaluator supplies the absolute beat of the active event onset; the engine measures each operation's slice grid from it.
- **Polyrhythm = per-lane loop rings** in the evaluator (events repeat every `loopBeats[op]` beats); no second clock or Scheduler.
- **Loop playback is beat-locked, not stretched.** `LoopSource` maps the timeline beat to a file position; capture starts on a bar line and records exactly N bars.
- **Phrase evolution, gestures, loop audio and history are session state** (not in the saved scene format) to keep saved projects and undo clean.
- **Locks apply to every edit path** (manual, preview, history recall, whole-scene operations); lock configuration itself stays editable.
- **Loop-service handoff** uses a reader-flag handshake plus a mutex against `prepareToPlay`; the audio thread never takes a lock.
