
## Approval record and immediate next actions

This research direction was approved for the next engineering step.

### Approved decisions

- Build architecture: JUCE + CMake
- Primary plugin target: VST3
- Standalone target: included as a development and validation target
- CLAP: deferred unless specifically required by the owner after validation
- Reference hosts: REAPER on Windows x64 and Linux x86_64
- MVP scope: bounded short-window rhythmic operations with phrase variations and transition policy
- Full phrase capture: explicitly out of scope for MVP
- Pitch-preserving stretch: deferred until later, if ever
- Product differentiation: workflow-first, not feature-count-first

### M0 technical spike plan

The next step is not a full product implementation. It is a small engineering spike that proves the stack and constraints before the core DSP work begins.

1. Create a minimal JUCE + CMake project structure
   - one audio processor
   - one editor shell
   - one standalone app stub
   - plugin exports for VST3 and standalone

2. Build and validate the plugin shell on the target systems
   - confirm JUCE project generation and build success
   - confirm plugin loads in REAPER on Windows and Linux
   - confirm parameter registration, state serialization, and process callback are stable

3. Validate the host and latency assumptions
   - measure process block size behavior in REAPER
   - confirm reported latency model
   - validate manual tempo and host transport fallback behavior

4. Prototype the bounded operations in a test harness
   - repeat/stutter with fixed buffer limits
   - reverse slice with limited window
   - gate and half-time feel using bounded sample windows
   - tape-stop-like rate ramp with explicit limits

5. Capture acceptance metrics
   - no allocations in the process callback
   - no UI calls from the audio thread
   - deterministic behavior over resets and tempo changes
   - no unbounded memory growth
   - clear latency reporting for any operation with look-ahead

### Exit criteria for the M0 spike

The spike is successful if:

- the JUCE/CMake project builds reliably
- the VST3 plugin loads in REAPER on both target OSes
- the standalone path is usable for debugging
- the core bounded operations can run without violating real-time safety assumptions
- the latency model is explicit and measurable
- the project can confidently move into the core DSP architecture without re-opening the stack questions

### Explicit rule for the next phase

No large feature work should begin until the M0 spike proves the stack and the bounded low-latency design is viable in the target hosts.

This is the point at which the project transitions from research to a focused engineering spike, not yet a full production implementation.
