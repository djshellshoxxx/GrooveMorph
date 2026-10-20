# macOS alpha builds

`.github/workflows/macos-alpha.yml` builds a universal macOS 11+ package on the
`macos-15` Apple Silicon runner for arm64 and Intel (x86_64). Each successful run produces separate
Standalone and VST3 ZIPs, their SHA-256 sidecars, and a combined convenience ZIP.
The workflow runs CTest and checks both binaries contain both architectures.

The packages are unsigned and not notarized. They are suitable for alpha
evaluation, not a signed consumer release. The Standalone application is inside
`GrooveMorph.app`. The VST3 bundle can be installed in
`~/Library/Audio/Plug-Ins/VST3/`, followed by a DAW rescan.

To publish, open **Actions → macOS Standalone and VST3 alpha → Run workflow** and
set `publish` to `true`. The workflow creates a macOS-specific prerelease and
attaches all packages and checksum files. Review the manifest and test log before
distributing an artifact. Automated builds do not verify DAW hosting, audio-device
playback, listening quality, code signing, notarization, or installer behavior.
