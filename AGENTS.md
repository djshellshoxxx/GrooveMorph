# GrooveMorph engineering instructions

The user authorizes full modular implementation from the approved specifications,
integration of verified work, audits, native builds, QA, and release preparation.
Read `skills/README.md`, the applicable `SKILL.md` files, and existing task ownership
before substantive work. Start with `skills/superpowers/using-superpowers/SKILL.md`.
DSP/JUCE work uses the audio DSP, numerics, JUCE, and VST3 review skills.
Use Caveman for concise communication when requested. Ultimate mode is opt-in.
Antigravity protocol skills are excluded. Skills do not override current user
authorization, project contracts, host instructions, or actual capabilities.

Inspect all active branches and PRs first. At this file's creation, newer M0 code
and `docs/SPECIFICATION.md`, `docs/SPEC_INDEX.md`, and `docs/TRACEABILITY.md` were on
`claude/awesome-keller-jx2ck0`, which contains `codex/m0-validation`; `main` still
held an older scaffold. Verify refs, review/test existing work, and integrate it
without duplicating implementation or overwriting another session's changes.

Follow C++20, CMake, and the pinned JUCE dependency. Current formats are VST3 and
JUCE Standalone on Windows x64/Linux x86_64. REAPER is the reference host. CLAP,
full phrase capture, and pitch-preserving stretch remain deferred unless the user
changes scope. Update specifications before changing public behavior/contracts.
Maintain requirement traceability and explicit verification evidence.

Keep domain models, phrase timing, scheduling, history, transforms, transitions,
and state services portable and separate from JUCE UI and platform adapters.
The callback must have bounded work and no allocation/deallocation, blocking,
I/O, logging, UI access, or unsafe object lifetime changes. Preallocate buffers,
cache parameter pointers, use verified bounded handoffs, and define reset,
queue-overflow, invalid-input, latency, bypass, and stopped-transport behavior.

After integrating the current M0 branch, the portable baseline is:
`cmake -S . -B build-core -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON`,
`cmake --build build-core --config Release`, and
`ctest --test-dir build-core --build-config Release --output-on-failure`.
Run full native Windows/Linux builds and affected DSP, concurrency, state, UI,
validator, and host checks. Core tests do not prove DAW compatibility or audio
quality. Record unavailable checks; never claim measurements not performed.

Use isolated ownership and short-lived branches. Preserve active work, merge
reviewed passing units promptly, and track blocked branches. Never force-push
another owner, delete unpreserved work, lower acceptance thresholds to pass,
or claim full release completion while mandatory gates remain unverified.
