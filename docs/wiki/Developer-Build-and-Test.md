# Developer Build and Test

## Toolchain

- CMake 3.22 or newer
- C++20 compiler. CI uses GCC and Clang on Linux, and MSVC on Windows
- Full plug-in build: fetches JUCE 8.0.7 on first configure. Pass `-DFETCHCONTENT_BASE_DIR` to keep the download somewhere stable
- Platform dependencies for the JUCE build are in `GETTING_STARTED.md`

## Portable core (no JUCE)

```sh
cmake -S . -B build-core -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release --parallel
ctest --test-dir build-core --build-config Release --output-on-failure --parallel 2
```

This builds `GrooveMorphCore` and the 29 standalone test executables in `tests/core/`.

## Full plug-in (VST3 and Standalone)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
```

Targets include `GrooveMorphPlugin` (VST3 and Standalone) and `GrooveMorphPluginTests`. `GrooveMorphEditorSnapshot` (from `tools/EditorSnapshot.cpp`) renders editor images for review.

## CI

- `build.yml`: the portable core on Linux (GCC and Clang) and the JUCE build
- `portable-core-sanitizers.yml`: the core under ASan and UBSan
- `windows-alpha.yml`: the Windows x64 Standalone alpha, with the portable core and tests
- `windows-standalone-smoke.yml`: launches the Standalone as a smoke test
- `macos-alpha.yml`: macOS alpha build
- `pluginval.yml`: plug-in validation

## Release and status

The alpha is not a release. `docs/alpha/ALPHA-FEATURE-STATUS.md` and `docs/alpha/BUILD-MANIFEST.md` say what each build contains. `docs/qa/DAW_AUDIO_RELEASE_READINESS.md` is the DAW acceptance matrix, and a release needs it done.

## Troubleshooting

- Core build fails with thread errors: the core links `Threads::Threads`. Check the CMake output for `find_package(Threads)`.
- JUCE fetch is slow or fails on first configure: keep `FETCHCONTENT_BASE_DIR` on a path that persists between runs.
- A new core source file is not compiled: re-run CMake (the glob uses `CONFIGURE_DEPENDS`, but a fresh configure is safest).
