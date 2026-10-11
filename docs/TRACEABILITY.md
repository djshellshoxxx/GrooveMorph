# Requirement traceability

This table tracks the current M0 slice. A module is not complete based on source presence alone; evidence must include successful tests/builds or an explicitly recorded unavailable check.

| Requirement | M0 behavior | Implementation | Verification | Status |
|---|---|---|---|---|
| GM-FR-001 phrase grid timing | Convert tempo, meter, phrase length and origin into sample-boundary offsets | `src/core/PhraseClock.*` | `tests/PhraseClockTests.cpp`; core CTest; later host transport tests | Implemented; core CTest passes via CMake on Linux GCC; MSVC CI pending |
| GM-FR-006 host timing fallback | Host tempo/meter/play state/position used when valid; invalid or absent fields fall back to manual tempo/meter parameters and a free-run sample counter; source and missing fields shown in editor | `src/core/TransportResolver.*`, `src/PluginProcessor.cpp`, `src/PluginEditor.cpp` | `tests/TransportResolverTests.cpp` (mocked absent/invalid context); REAPER transport tests in M0 gate | Core tests pass on Linux GCC; plugin builds on Linux; host tests pending |
| GM-FR-007 stable automation/state | Parameterized M0 gain path plus manual tempo/meter/phrase-bar parameters with JUCE APVTS state | `src/PluginProcessor.*` | `tests/PluginProcessorTests.cpp` (headless state round-trip, malformed state); VST3 automation/state restore tests in REAPER | Linux build and headless state tests pass; host check pending |
| GM-FR-008 MIDI trigger | Plugin declares MIDI input; scene trigger mapping not yet implemented | `src/PluginProcessor.*` | Host MIDI-routing test after trigger feature | Not implemented beyond port declaration |
| GM-QR-001 real-time safety | Phrase-clock math is pure and allocation-free; gain path uses preexisting parameter state | `src/core/PhraseClock.*`, `src/PluginProcessor.cpp` | Review + sanitizer/thread checks + audio stress | Partial; runtime verification pending |
| GM-QR-005 timing | Boundary math follows sample-domain grid and validates input ranges | `src/core/PhraseClock.*` | deterministic grid tests; REAPER sample-offset tests later | Core tests passed locally; host tests pending |
| GM-QR-006 latency | M0 gain path reports zero algorithmic latency | `src/PluginProcessor.cpp` | `tests/PluginProcessorTests.cpp`; VST3 host/PDC validation | Headless test passes; host validation pending |
| GM-QR-004 click-free parameter changes | Gain changes ramp linearly across each block | `src/PluginProcessor.cpp` | `tests/PluginProcessorTests.cpp` ramp check; audio sweep tests later | Headless test passes |
| GM-QR-011 portability | Phrase-clock module is standard-library-only | `src/core/PhraseClock.*` | GCC and MSVC build via CI | GCC test passed; CMake/MSVC pending |

## Known deviations and limitations

- GM-FR-005: the M0 spike uses `masterIntensity` as a plain output gain, so zero intensity is silence rather than the dry path. This must change when the first wet transformation lands (M3).
- Phrase boundaries are computed from host sample position relative to sample 0. Hosts with tempo changes or bar offsets need PPQ/bar-start alignment (`ppqPositionOfLastBarStart`) before scheduling is used musically (M1).
- The resolved `isPlaying` flag is not yet consumed; nothing is scheduled.

## Pending M0 integration checks

- CMake configure/build for VST3 and JUCE Standalone on Windows (Linux build verified locally with GCC/Ninja).
- VST3 plugin scan and load in REAPER on both target operating systems.
- Parameter automation and project save/reopen.
- Reported zero latency and mono/stereo behavior.
- No allocations/locks in the audio callback under instrumentation.
- Standalone device open and audio pass-through behavior.
