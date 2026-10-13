# Getting Started: GrooveMorph integrated MVP

This guide builds the current JUCE VST3 and Standalone targets. CLAP is deferred by the approved project direction.

## Prerequisites

- Git
- CMake 3.22 or later
- C++20 toolchain: Visual Studio 2022 with Desktop development with C++ on Windows; GCC/Clang on Linux
- Internet access for the first full build so CMake can fetch JUCE 8.0.7
- Linux development packages: ALSA, JACK, FreeType, X11 and GTK 3 headers (`libasound2-dev libjack-jackd2-dev libfreetype6-dev libx11-dev libxcomposite-dev libxcursor-dev libxinerama-dev libxrandr-dev libxext-dev libxrender-dev libgtk-3-dev` on Ubuntu)

## Clone and configure

```sh
git clone https://github.com/djshellshoxxx/GrooveMorph.git
cd GrooveMorph
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

To build only the portable timing, scheduling, DSP, scene/state core and tests, without downloading JUCE:

```sh
cmake -S . -B build-core -DGROOVEMORPH_CORE_ONLY_BUILD=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release
ctest --test-dir build-core --build-config Release --output-on-failure
```

## Build all configured targets

```sh
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

The JUCE plugin target produces VST3 and Standalone variants. Generated artifact directories are under `build/GrooveMorphPlugin_artefacts/Release/`; the exact format subdirectory can vary by generator/platform. Locate the `.vst3` bundle or Standalone executable there.

## Windows Visual Studio generator

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

## Verified builds and validation still required

Windows/Linux VST3 and Standalone builds, portable sanitizers and Windows
five-second startup smoke passed for runtime commit 7f2784a. All eight matching
Linux test executables were rerun successfully on 2026-10-13. See
[exact CI and artifact links](docs/TIMING_INTEGRATION_HANDOFF.md).

The Windows CI download contains the Standalone executable, not a packaged VST3
release or installer. Build the VST3 bundle with the commands above. Verify scan,
MIDI routing, automation, project recall, PDC and actual audio-device operation in
REAPER before claiming host compatibility. No completed production release is implied.

For standalone audio processing without a host playhead, choose Sample Clock and
set manual BPM/meter. Automatic without host play state remains stopped. An explicitly
stopped host remains stopped in every mode. See [timing help](help/TIMING.md).
