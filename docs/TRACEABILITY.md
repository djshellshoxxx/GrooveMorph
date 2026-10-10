# Requirement traceability

This table tracks the current M0 slice. A module is not complete based on source presence alone; evidence must include successful tests/builds or an explicitly recorded unavailable check.

| Requirement | M0 behavior | Implementation | Verification | Status |
|---|---|---|---|---|
| GM-FR-001 phrase grid timing | Convert tempo, meter, phrase length and origin into sample-boundary offsets | `src/core/PhraseClock.*` | `tests/PhraseClockTests.cpp`; core CTest; later host transport tests | Implemented; local compiler test passed; CMake/CI pending |
| GM-FR-006 host timing fallback | M0 shell has no host transport scheduler yet | Future `PhraseClock`/plugin adapter | REAPER transport tests in M0 gate | Not implemented |
| GM-FR-007 stable automation/state | Parameterized M0 gain path with JUCE APVTS state | `src/PluginProcessor.*` | VST3 automation/state restore tests in REAPER | Code present; JUCE build/host check pending |
| GM-FR-008 MIDI trigger | Plugin declares MIDI input; scene trigger mapping not yet implemented | `src/PluginProcessor.*` | Host MIDI-routing test after trigger feature | Not implemented beyond port declaration |
| GM-QR-001 real-time safety | Phrase-clock math is pure and allocation-free; gain path uses preexisting parameter state | `src/core/PhraseClock.*`, `src/PluginProcessor.cpp` | Review + sanitizer/thread checks + audio stress | Partial; runtime verification pending |
| GM-QR-005 timing | Boundary math follows sample-domain grid and validates input ranges | `src/core/PhraseClock.*` | deterministic grid tests; REAPER sample-offset tests later | Core tests passed locally; host tests pending |
| GM-QR-006 latency | M0 gain path reports zero algorithmic latency | `src/PluginProcessor.cpp` | VST3 host/PDC validation | Code sets zero; host validation pending |
| GM-QR-011 portability | Phrase-clock module is standard-library-only | `src/core/PhraseClock.*` | GCC and MSVC build via CI | GCC test passed; CMake/MSVC pending |

## Pending M0 integration checks

- CMake configure/build for VST3 and JUCE Standalone on Windows and Linux.
- VST3 plugin scan and load in REAPER on both target operating systems.
- Parameter automation and project save/reopen.
- Reported zero latency and mono/stereo behavior.
- No allocations/locks in the audio callback under instrumentation.
- Standalone device open and audio pass-through behavior.
