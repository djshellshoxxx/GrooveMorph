# Expansion dependency graph

```mermaid
flowchart TD
 M0[M0 MVP scheduler integration] --> M1[M1 Canonical time + scenes + state]
 M1 --> F08[F08 History]
 M1 --> F06[F06 Lane timing]
 F08 --> F01[F01 Variation]
 F01 --> F05[F05 Evolution]
 F08 --> F11[F11 Gesture]
 F06 --> F03[F03 Groove protection]
 F06 --> F02[F02 Morph]
 F08 --> F02
 F02 --> F04[F04 Performance]
 F04 --> F07[F07 Audio triggers]
 F04 --> F11
 F03 --> F10[F10 Multiband]
 F08 --> F09[F09 Import/capture]
 F09 --> F10
 F04 --> F12[F12 Host/MIDI/CLAP]
 ```

Contract freeze order: MusicalTime -> SceneBank migration adapters -> History -> EventScheduler -> F01 / F06 -> remaining planners -> host/UI adapters. This dependency DAG forbids concurrent ownership of PluginProcessor, PluginEditor, core state schema and global CMake. Scheduler PPQ semantics need merging from PR #4 before the phase M1 contract is frozen. An implementation agent must compare the actual mainline head with the integration head before changing these dependencies.
