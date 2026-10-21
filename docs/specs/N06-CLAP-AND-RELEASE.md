# N06 CLAP format and beta release packaging

Status: specification, written before implementation (2026-10-20). Owner: worker W9.
Nothing here has been run on a CI runner at the time of writing; see "Verification by CI".

## 1. Objective

1. Add the CLAP plugin format to the JUCE 8.0.7 build through `free-audio/clap-juce-extensions`.
2. Provide ONE new workflow, `.github/workflows/beta-release.yml`, that builds, tests, packages and
   (optionally) publishes a GitHub PRERELEASE for four platform packages:
   Windows x64, macOS arm64, macOS x86_64, Linux x86_64.
3. Make the version a single source of truth (`VERSION` file) so the tag, the CMake project
   version, the plugin metadata and the compiled-in version string cannot disagree.
4. Ship installation instructions, third-party notices and honest release notes.

Non-goals: code signing, notarization, installers, universal macOS binaries, AU/AAX/LV2, Linux arm64,
Windows arm64, DAW or listening validation, changing any DSP/UI behaviour.

## 2. CLAP format

| Item | Value |
|---|---|
| Adapter | `free-audio/clap-juce-extensions`, MIT |
| Pin | commit `7adee3a1bd4684d4caa5601100e364abccff4b4d` (main, 2026-10-06). Upstream release tags (0.24-0.26) date from 2022 and predate JUCE 8, so a tag is not usable. |
| Fetch | `FetchContent`, submodules recursive (`clap`, `clap-helpers`), no `GIT_SHALLOW` (a shallow clone cannot target a bare commit reliably) |
| Option | `GROOVEMORPH_ENABLE_CLAP`, default ON, only meaningful when `GROOVEMORPH_CORE_ONLY_BUILD=OFF` |
| CLAP id | `com.djshellshoxxx.groovemorph` |
| Features | `audio-effect`, `glitch`, `stereo` (all standard CLAP feature strings) |
| Output | `<build>/GrooveMorphPlugin_artefacts/<Config>/CLAP/GrooveMorph.clap` (file on Windows/Linux, bundle on macOS) |
| Target | `GrooveMorphPlugin_CLAP`, created by `clap_juce_extensions_plugin(TARGET GrooveMorphPlugin ...)` |

Pins live in `cmake/GrooveMorphPins.cmake` (JUCE tag, adapter commit) so CI cache keys can hash one file.

Invariants:
- `-DGROOVEMORPH_CORE_ONLY_BUILD=ON` never fetches JUCE or the adapter; core tests are unchanged.
- `-DGROOVEMORPH_ENABLE_CLAP=OFF` produces exactly the previous VST3 + Standalone build.
- No parameter ID, state key or processor code changes. The adapter wraps the existing
  `juce::AudioProcessor`, so parameters, state and MIDI input reuse the same code paths as VST3.
- Adapter options left at upstream defaults (`CLAP_CHECKING_LEVEL=Minimal`, no sample-accurate event
  splitting). Sample-accurate automation in CLAP is therefore NOT claimed.

Known risks (unverified until CI runs): adapter commit compiling against JUCE 8.0.7 with MSVC,
Apple clang and GCC 11; `GrooveMorphPluginTests` linking the static `clap_juce_extensions` library
that the adapter attaches to the plugin target; the `$<CONFIG>`-based CLAP output path.

## 3. Versioning (single source)

- `VERSION` (repo root), one line: `MAJOR.MINOR.PATCH` or `MAJOR.MINOR.PATCH-beta.N`. Initial value `0.2.0-beta.1`.
- CMake reads it before `project()`. `project(GrooveMorph VERSION <MAJOR.MINOR.PATCH>)` and the
  `juce_add_plugin(VERSION ...)` use the numeric part (JUCE and macOS bundle metadata need `X.Y.Z`).
- A generated header `groovemorph/Version.h` (from `cmake/Version.h.in`, on the include path of
  `GrooveMorphCore`) defines `GROOVEMORPH_VERSION_STRING` (full string), `GROOVEMORPH_VERSION_NUMERIC`
  and `GROOVEMORPH_VERSION_PRERELEASE`. UI authors may display it; this task does not edit the UI.
- Tag = `v` + contents of `VERSION`. The workflow `prepare` job fails if a pushed tag differs from
  `v$(cat VERSION)`, or if the dispatch input `version` is non-empty and differs from `VERSION`.
  To release: edit `VERSION`, commit, then tag or dispatch.
- Zip names: `GrooveMorph-<version>-<platform>.zip`, platform in
  `windows-x64`, `macos-arm64`, `macos-x86_64`, `linux-x86_64`.

## 4. Workflow `beta-release.yml`

Triggers: `workflow_dispatch` (inputs `version` string optional, `publish` boolean default false) and
`push` of tags matching `v*-beta*`. No push-to-branch trigger and NO commit-message trigger (the
alpha `[publish-alpha]` trigger is deliberately not reused). Top-level `permissions: contents: read`;
only the `release` job has `contents: write`. `concurrency` per version, never cancelling a release.

Jobs:
1. `prepare` (ubuntu-24.04): validate version/tag, expose `version`, `tag`, `publish`.
   `publish` is true for tag pushes, and for dispatch only when the input is ticked.
2. `build` (matrix, `fail-fast: false`):

| Platform | Runner | Notes |
|---|---|---|
| windows-x64 | `windows-2022` | MSVC, Release. Standalone `.exe`, VST3 bundle, `.clap` |
| macos-arm64 | `macos-15` | native arm64, `CMAKE_OSX_ARCHITECTURES=arm64`, deployment target 11.0 |
| macos-x86_64 | `macos-15-intel` | native Intel, `CMAKE_OSX_ARCHITECTURES=x86_64`, deployment target 11.0 |
| linux-x86_64 | `ubuntu-22.04` | glibc 2.35 baseline |

   Runner research (2026-10-20): `macos-13` was retired in December 2025; GitHub documents
   `macos-15-intel` and `macos-26-intel` as the Intel images (Intel images end August 2027). Separate
   native builds were chosen over one universal/cross-compiled build because the task requires two packages and
   native runs let `ctest` execute the real architecture. `ubuntu-22.04` is documented as
   deprecated from 2026-09-17 and unsupported from 2027-04-17; it still works now and is required for the
   glibc floor. When it is retired, move that one matrix entry to a `container: ubuntu:22.04`
   job on `ubuntu-24.04` (not done now, to avoid an untested container path).

   Steps per leg: checkout; (Linux) apt dependencies; restore the FetchContent cache
   (`actions/cache`, key from runner OS, arch, and the hash of `cmake/GrooveMorphPins.cmake`);
   configure (`-DBUILD_TESTING=ON`, `-DGROOVEMORPH_ENABLE_CLAP=ON`); build
   (`--parallel`); `ctest --output-on-failure`; assert the architecture of macOS binaries with `lipo -archs`;
   run `clap-validator` 0.4.1 (pinned release asset with pinned SHA-256) against the built `.clap`
   (non-blocking, result recorded); package with `tools/package/package.sh` (or `package.ps1` on Windows);
   upload the zip, its `.sha256` and the validator log as artifacts.
3. `release` (ubuntu-24.04, only when `publish`): download all artifacts, write `SHA256SUMS`,
   generate `RELEASE-NOTES.md` with `tools/package/make-release-notes.sh`, then create (or update, when the
   release already exists) a GitHub prerelease titled `GrooveMorph <version> (Beta)` with every zip,
   every `.sha256`, `SHA256SUMS` and `RELEASE-NOTES.md`. Always `--prerelease`, never "latest".

## 5. Package contents (each zip)

Top folder `GrooveMorph-<version>-<platform>/` containing: the Standalone (`GrooveMorph.exe`,
`GrooveMorph.app`, or `GrooveMorph` on Linux), `GrooveMorph.vst3`, `GrooveMorph.clap`, `INSTALL.md`,
`THIRD-PARTY-NOTICES.md`, `KNOWN-ISSUES.md`, `QUICK-START.md`, `BUILD-MANIFEST.md`
(version, commit, runner, test and validator outcomes, SHA-256 of each binary), `ctest.log`,
`clap-validator.log`, `VERSION`. The packaging scripts fail if any expected artefact is missing.

macOS bundles are ad-hoc signed (`codesign --sign -`) so arm64 code runs at all; they are NOT Developer
ID signed and NOT notarized. Users must clear quarantine (documented in `docs/beta/INSTALL.md`).
Windows binaries are unsigned. The Windows build links the MSVC runtime dynamically (CMake default);
`INSTALL.md` says the Microsoft Visual C++ Redistributable (x64) may be required. This is unverified.

## 6. Release notes (generated)

`RELEASE-NOTES.md` is assembled by script, never hand-edited at release time. Sections: what this is;
a fixed statement that NO DAW, audio-hardware or listening validation has been performed and that
automated results show only that the code compiled, core and processor tests passed, and the CLAP
validator outcome; per-platform validator outcomes; packaging limitations (unsigned, not notarized, no
installer, untested install paths); the full verbatim text of `docs/alpha/KNOWN-ISSUES.md` at the
built commit; the licensing statement (no project licence declared; JUCE terms apply); checksums.

## 7. Licensing statement (do not pick a licence)

`THIRD-PARTY-NOTICES.md` states that: the project owner has not declared a licence for GrooveMorph;
JUCE 8 modules are dual-licensed AGPLv3 or the commercial JUCE 8 licence, so the owner must choose a
project licence compatible with one of those before any public distribution beyond testers; this task does
not choose one. It lists JUCE and its bundled dependencies per JUCE 8.0.7 `LICENSE.md`, and
clap-juce-extensions, CLAP and clap-helpers (MIT, with copyright lines).

## 8. Acceptance criteria

| ID | Criterion | Evidence |
|---|---|---|
| N06-A1 | Core-only configure/build/test is unchanged (all existing core tests pass) | local ctest |
| N06-A2 | `VERSION` parse rejects malformed strings; `GROOVEMORPH_VERSION_STRING` matches `VERSION` and starts with the CMake project version | `tests/core/VersionTests.cpp` (local) |
| N06-A3 | Plugin configure with CLAP ON creates `GrooveMorphPlugin_CLAP`; with OFF it does not | CI configure logs |
| N06-A4 | All four legs build, `ctest` passes | CI |
| N06-A5 | `.clap` exists per leg and `clap-validator` outcome is recorded | CI logs, manifest |
| N06-A6 | macOS legs: binaries contain exactly the leg's architecture | CI `lipo -archs` |
| N06-A7 | Tag/input/`VERSION` mismatch fails `prepare` | CI (negative dispatch run) |
| N06-A8 | Release is a prerelease with 4 zips, 4 `.sha256`, `SHA256SUMS`, `RELEASE-NOTES.md` | CI + release page |
| N06-A9 | Workflow cannot publish from a commit message; `contents: write` only on `release` | review of YAML |
| N06-A10 | Scripts syntax-check (`bash -n`, shellcheck if available, PowerShell parser) | local where possible |
| N06-A11 | CLAP loads and behaves in a real host | NOT VERIFIABLE by CI; manual (REAPER/Bitwig), not done |

## 9. Superseded workflows

`windows-alpha.yml` and `macos-alpha.yml` stay in place and working, but are superseded for releases by
`beta-release.yml`. They still carry the `[publish-alpha]` commit-message trigger and their own
`0.1.0-alpha.*` versioning; do not use them for beta. `build.yml`, `pluginval.yml`,
`portable-core-sanitizers.yml` and `windows-standalone-smoke.yml` remain the regular CI.
Documented in `docs/beta/RELEASE-PROCESS.md`.

## 10. Verification by CI (cannot be done locally: no JUCE, 2 cores, Linux only)

See the final report; the list is mirrored in `docs/beta/RELEASE-PROCESS.md`.
