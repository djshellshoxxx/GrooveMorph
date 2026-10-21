# GrooveMorph Wiki

GrooveMorph is a phrase-aware rhythmic audio effect. It takes the audio running through it and rearranges the rhythm of each bar with five transformations: repeat, reverse slice, gate, half-time and tape stop. Four editable variations (A to D) let you switch between treatments on the bar line.

**Status:** alpha, Windows x64 Standalone and VST3 builds. Exploratory, not beta and not production. Nothing has been tested in a DAW or on audio hardware yet. See `docs/alpha/ALPHA-FEATURE-STATUS.md`.

**Who it is for:** electronic producers and DJs who want controlled, repeatable rhythmic edits on a loop or a full mix, and developers who want to see how the phrase engine is built.

## Quick start

```sh
cmake -S . -B build-core -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

The full VST3 and Standalone targets fetch JUCE 8.0.7 on first configure. See [[Developer-Build-and-Test]].

## Pages

**For users**
- [[User-Guide]]: first run, timing, variations, the five operations, MIDI, troubleshooting

**For developers**
- [[Developer-Overview]]: architecture, design decisions, data and thread model
- [[Developer-Code-Map]]: files, classes, and where to change what
- [[Developer-Build-and-Test]]: build targets, tests, CI
- [[Developer-Conventions]]: real-time rules, portability, gotchas

**Repo docs (authoritative)**
- `help/` (illustrated user guide): `QUICK-START.md`, `CONTROLS.md`, `TIMING.md`, `WORKFLOWS.md`, `TROUBLESHOOTING.md`
- `docs/SPECIFICATION.md`, `docs/TRACEABILITY.md`, `docs/TODO.md`
- `docs/alpha/` (feature status, known issues, user test guide)
- `GETTING_STARTED.md`, `RESEARCH_NOTES.md`, `AGENTS.md`
