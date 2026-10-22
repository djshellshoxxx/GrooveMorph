# MIDI policy continuation — 2026-10-22

Base: main `bdb9cb5`. Branch: `codex/midi-policy-completion-20261022`.

## Changes and evidence

- Reproduced a full-table MIDI state defect: deserialization stopped at 64 links,
  ignoring later updates to existing targets. The new regression failed before
  the fix and passes afterward; excess new targets remain rejected.
- A04-T07: learn filters CC 20–22 while macros are enabled; direct processor links
  reject these CCs with a distinct notice. Macros off permits those links.
- A04-T08: existing explicit links suppress their corresponding built-in macro
  slot on the message thread. Pending macro writes are discarded when disabled.
- A04-T09: linked-control menus list the other target IDs sharing a CC and explain
  an explicit macro override. Input tooltip explains the reservation policy.
- No new state keys or audio-callback work, allocation, or locks were introduced.
  Macro conflict resolution and description construction run on message-thread services.

## Checks performed

Portable Release build with GCC 13.3.0, CMake 4.4.4 on Linux:

```sh
cmake -S . -B build-core -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-core -j 4
ctest --test-dir build-core --output-on-failure -j 2
```

56/56 tests passed in 24.75 seconds. This includes the expanded MidiLinksTests.
The workspace initially dropped execute permission on several generated test
binaries; restoring execute bits allowed the full suite to run successfully.

Processor regressions were added to PluginProcessorTests but have NOT run locally.
Native JUCE build dependencies are absent and package installation is blocked by
an unavailable dpkg lock. Windows/Linux CI must compile and execute these checks.
UI interaction, host automation, audio device and listening tests were not performed.

The completeness matrix was regenerated. It still reports nine pre-existing
UI-REDESIGN requirement gaps and a stale N02 PresetBar UI reference; no blanket
completeness claim is made. Soft pickup remains open under B-05/F12-T11.
