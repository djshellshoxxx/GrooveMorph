# Canonical expansion architecture (proposed)

```mermaid
flowchart TD
 UI[Quick / Editor / Performance UI] --> CMD[Command bridge]
 CMD --> MODEL[Immutable canonical SceneModel]
 MODEL --> HIST[History / Version store]
 MODEL --> PLAN[Background variation / evolution / morph planning]
 PLAN --> VALID[Constraint + bounds validator]
 VALID --> PUB[Versioned fixed-capacity schedule publication]
 HOST[VST3 / Standalone / CLAP adapters] --> CLOCK[Musical time resolver]
 CLOCK --> RT[Bounded event scheduler]
 PUB --> RT
 RT --> DSP[Transformation + transition + optional multiband]
 DSP --> OUT[Audio output]
 ```

Portable core: rational musical time, versioned scene/event model, scheduler, planner, deterministic PRNG, constraints, history and serialization. JUCE adapters: host transport, APVTS bridge, MIDI/sidechain layout, editor and packaging. The audio processor owns preallocated DSP scratch, ring buffers and bounded plans. UI/background threads exclusively own filesystem, allocation, audio decoding, history mutation and high-cost planning. The real-time thread never destroys published snapshot ownership or blocks for a producer. Prefer a bounded generation-stamped mailbox or verified SPSC handoff; exact scheme must be proven against multi-producer use and lifecycle churn.

## Service boundaries
- SceneModel: immutable scene/events and stable UUIDs, never direct JUCE ValueTree dependency.
- MusicalTime: normalized host PPQ and sample position with explicit validity flags and epoch for host seeks.
- EventScheduler: deterministic ordered event dispatch in fixed sample horizon.
- ConstraintEngine: mandatory validation shared by history, generation, morph and evolution.
- TransformationEngine: render bounded operation plan; no unbounded lookahead.
- TransitionMixer: equal-power or other explicitly chosen curves with dry/processed delay alignment.
- StateCodec: versioned, authenticated-by-bounds parser, migration registry and atomic acceptance.
- StatusPublisher: atomics/lock-free bounded status snapshots, no UI callbacks on audio thread.

## Design decisions pending evidence
Do not select snapshot reclamation algorithm, state binary encoding, minimum-phase crossover coefficients, or CLAP adapter solely from this prose. Benchmark candidates and record design decision with deterministic tests before freezing public headers.