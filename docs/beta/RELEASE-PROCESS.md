# Beta release process

Spec: [N06-CLAP-AND-RELEASE](../specs/N06-CLAP-AND-RELEASE.md). Workflow: `.github/workflows/beta-release.yml`.

## Cutting a beta

1. Edit `VERSION` (for example `0.2.0-beta.2`) and commit. This file is the only version source.
2. Either push the matching tag (publishes automatically):
   `git tag v0.2.0-beta.2 && git push origin v0.2.0-beta.2`
   or run the workflow by hand (Actions, Beta release, Run workflow) on that commit and tick `publish`.
   The `platforms` input (default `all`) accepts `all` or `windows`. With `windows` only the Windows x64
   package is built and published (the macOS and Linux legs are not created); use it while those legs are not
   ready. Tag pushes always use `all`.
   Leaving `publish` unticked builds and uploads artifacts only. The `version` input may be left empty; if set
   it must equal `VERSION`, otherwise the run fails in its first job.
3. The `release` job runs only if every selected platform build succeeded. It creates (or updates) the GitHub
   PRERELEASE `v<VERSION>` titled `GrooveMorph <VERSION> (Beta)` with the zips of the selected platforms (four, or the Windows one), their `.sha256` files,
   `SHA256SUMS` (made from the zips that exist) and the generated `RELEASE-NOTES.md`. A `windows` release's notes
   list the platforms included and say the macOS and Linux builds will follow. Running again with `all` on the same
   commit adds the other platforms to the same prerelease and replaces the notes and `SHA256SUMS`.
4. Release notes template: the generated notes always list the included platforms and keep the "no DAW or listening
   validation" statements. For beta.2 also mention the redesigned interface (see `help/INTERFACE-TOUR.md`) and that it
   was reviewed from CI screenshots only (TODO B-13).
5. Read the generated notes before telling anyone about the release. They state that no DAW or listening
   validation was done; that stays true until a person does it and the notes script is changed to say so.

## UI snapshots

`build.yml` has a `ui-snapshots` job (every push and PR) that builds `GrooveMorphEditorSnapshot` on Linux, renders the
Studio view at four sizes, every inspector tab, Easy mode, the preset browser and the Visual window under xvfb, and
uploads the PNGs as the `ui-snapshots` artifact (14 days). The tool exits non-zero if a PNG is empty or one flat
colour. Locally: `GrooveMorphEditorSnapshot [outdir]` (default `docs/beta/screenshots`). It is not part of the
Windows beta job.

A commit message never triggers a release, and branch pushes never build packages.

## Superseded workflows

| Workflow | Status |
|---|---|
| `.github/workflows/windows-alpha.yml` | Superseded by `beta-release.yml` for releases. Left in place and working. Still publishes on `[publish-alpha]` in a commit message and uses `0.1.0-alpha.<run>` versions. Do not use for beta. |
| `.github/workflows/macos-alpha.yml` | Superseded by `beta-release.yml` for releases (universal arm64+x86_64 alpha package; the beta ships separate packages). Same `[publish-alpha]` caveat. |
| `build.yml`, `pluginval.yml`, `portable-core-sanitizers.yml`, `windows-standalone-smoke.yml` | Unchanged regular CI. Not superseded. |

## What only a CI run can verify (not checked locally; no JUCE, Linux-only machine)

- [ ] The pinned clap-juce-extensions commit configures and compiles against JUCE 8.0.7 on MSVC, Apple clang (arm64 and x86_64) and GCC 11 (Ubuntu 22.04), and produces `GrooveMorph.clap` under `GrooveMorphPlugin_artefacts/Release/CLAP/`.
- [ ] `GrooveMorphPluginTests` and `GrooveMorphEditorSnapshot` still link with the adapter attached to the plugin target; all ctest tests pass on all four legs (`VersionTests` included).
- [ ] The whole plugin tree still builds with GCC 11 / C++20 (before this task it was only built with GCC 13 on Linux).
- [ ] FetchContent of a bare commit with recursive submodules works on all runners; the cache restore/save works with the `.fetch` directory.
- [ ] `runs-on: macos-15-intel` and `ubuntu-22.04` are available to the repository; `lipo -archs` equals the leg's architecture for every binary.
- [ ] `codesign --force --deep --sign -` succeeds on the `.app`, `.vst3` and `.clap` bundles.
- [ ] `clap-validator` downloads, passes its checksum, runs on each OS, and what it reports (the verdict is non-blocking and written into manifests and notes).
- [ ] `package.ps1` runs under the runner's PowerShell 7 (it was only read, never executed); the zip it makes has forward-slash entries.
- [ ] A tag push and a dispatch run both pass `check-version.sh`; a mismatching tag/input fails; a dispatch with `publish` creates the prerelease with all assets; re-running updates it instead of failing.
- [ ] The release is flagged as pre-release and is not "latest".
- [ ] Not verifiable by CI at all: loading the VST3/CLAP in any DAW, audio output, install steps in `INSTALL.md` on clean machines, Gatekeeper/SmartScreen behaviour, VC++ redistributable need.

## Known follow-ups

- `ubuntu-22.04` runner retirement (2027-04-17): move the Linux leg to a `ubuntu:22.04` container.
- `macos-15-intel` runner retirement (August 2027): the Intel package then needs a cross-compile on an arm64 runner (`CMAKE_OSX_ARCHITECTURES=x86_64`), which cannot run ctest natively.
- `AGENTS.md` and `docs/SPECIFICATION.md` still say CLAP is deferred; the integrator should update them.
- The compiled version string (`GROOVEMORPH_VERSION_STRING`) is available but no UI shows it yet.
