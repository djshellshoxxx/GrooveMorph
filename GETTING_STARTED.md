# Getting Started: GrooveMorph M0

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

To build only the portable phrase-clock core and tests, without downloading JUCE:

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

## M0 validation

The current implementation is a timing and stack spike. Validate plugin scanning, state recall, automation, mono/stereo layout, zero-latency reporting, and standalone audio-device behavior in REAPER before treating the spike as complete. A successful compilation alone does not complete M0.
