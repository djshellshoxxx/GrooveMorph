# Installing GrooveMorph (beta)

This is a beta for testers. **No DAW, audio-device or listening validation has been done**, and these install steps have not been run on a clean machine by a person. No installer exists: you copy files. Lower your monitor volume the first time you load it.

Each zip is named `GrooveMorph-<version>-<platform>.zip` and holds `GrooveMorph` (Standalone), `GrooveMorph.vst3`, `GrooveMorph.clap`, this file, `THIRD-PARTY-NOTICES.md`, `KNOWN-ISSUES.md` and `BUILD-MANIFEST.md`. Pick the zip for your system:

| Zip platform | System |
|---|---|
| `windows-x64` | Windows 10/11, 64-bit Intel/AMD |
| `macos-arm64` | Apple Silicon (M1 and later) |
| `macos-x86_64` | Intel Macs |
| `linux-x86_64` | 64-bit Linux, glibc 2.35 or newer (Ubuntu 22.04 or newer, comparable distros) |

Verify the download first: compare the zip's SHA-256 with the `.sha256` file or `SHA256SUMS` on the release page.

## Where plugins go

| Format | Windows | macOS | Linux |
|---|---|---|---|
| VST3 (per user) | `%LOCALAPPDATA%\Programs\Common\VST3\` | `~/Library/Audio/Plug-Ins/VST3/` | `~/.vst3/` |
| VST3 (all users) | `C:\Program Files\Common Files\VST3\` | `/Library/Audio/Plug-Ins/VST3/` | `/usr/lib/vst3/` |
| CLAP (per user) | `%LOCALAPPDATA%\Programs\Common\CLAP\` | `~/Library/Audio/Plug-Ins/CLAP/` | `~/.clap/` |
| CLAP (all users) | `C:\Program Files\Common Files\CLAP\` | `/Library/Audio/Plug-Ins/CLAP/` | `/usr/lib/clap/` |

Some hosts only scan the all-users folders, or let you add custom scan paths. After copying, restart the host and rescan. `GrooveMorph.vst3` is a folder (bundle): copy the whole folder. On macOS, `.vst3`, `.clap` and `.app` are bundles too.

## Windows

1. Unzip anywhere.
2. Standalone: run `GrooveMorph.exe`. Windows SmartScreen may warn because the file is unsigned: More info, then Run anyway. In the Standalone, open Options, then Audio/MIDI Settings and choose an output device. Tick "Demo loop" in the top bar to hear a built-in loop without any input.
3. Plugins: copy the `GrooveMorph.vst3` folder and/or `GrooveMorph.clap` file into the folders above.
4. If a "VCRUNTIME140.dll was not found" style error appears, install the Microsoft Visual C++ Redistributable (x64). Whether it is needed has not been verified.

## macOS (arm64 and Intel)

The builds are only ad-hoc signed, not notarized, so Gatekeeper blocks them until you remove the quarantine flag. Run this in Terminal from the folder where you unzipped (label: clear the quarantine flag from all GrooveMorph items, then copy the plugins into your user folders):

```sh
cd ~/Downloads/GrooveMorph-*-macos-*/
xattr -dr com.apple.quarantine .
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/CLAP
cp -R GrooveMorph.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R GrooveMorph.clap ~/Library/Audio/Plug-Ins/CLAP/
```

Run `GrooveMorph.app` (drag it to Applications if you like). Use the package that matches your Mac; a host running under Rosetta needs the Intel (`x86_64`) plugin, not the arm64 one. If macOS still refuses, open System Settings, Privacy and Security, and choose Open Anyway.

## Linux

Dependencies: ALSA and X11 libraries are normally present on a desktop install. If the Standalone does not start, run it from a terminal and read the missing-library message. Label: unzip, mark the Standalone executable, and copy the plugins into your user folders:

```sh
unzip GrooveMorph-*-linux-x86_64.zip
cd GrooveMorph-*-linux-x86_64
chmod +x GrooveMorph
mkdir -p ~/.vst3 ~/.clap
cp -r GrooveMorph.vst3 ~/.vst3/
cp GrooveMorph.clap ~/.clap/
./GrooveMorph
```

Plugin GUIs use X11. Under a Wayland session the host must run with XWayland. Audio goes through ALSA (and JACK if you run it); PipeWire users normally go through its ALSA/JACK compatibility.

## Uninstalling

Delete the files you copied. GrooveMorph writes no installer entries. It keeps local generator history and settings in the usual per-user application data folder of your OS; deleting that is optional.

## Reporting a problem

Include the version (the file `VERSION` in the zip), your OS and version, the host and its version, the format (Standalone, VST3 or CLAP), and the steps. Attach the host's log if the plugin failed to load.
