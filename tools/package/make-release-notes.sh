#!/usr/bin/env bash
# Generate RELEASE-NOTES.md for the beta prerelease from the per-platform *.status.txt files that
# package.sh / package.ps1 wrote, plus the verbatim known-issues file at the built commit.
#
# Usage: make-release-notes.sh --version V --commit SHA --run-url URL --dist-dir DIR --output FILE
set -euo pipefail

version="" commit="" run_url="" dist="" output=""
while [ $# -gt 0 ]; do
  case "$1" in
    --version) version="$2"; shift 2 ;;
    --commit) commit="$2"; shift 2 ;;
    --run-url) run_url="$2"; shift 2 ;;
    --dist-dir) dist="$2"; shift 2 ;;
    --output) output="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
for v in version commit dist output; do [ -n "${!v}" ] || { echo "missing --$v" >&2; exit 2; }; done
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

platforms=(windows-x64 macos-arm64 macos-x86_64 linux-x86_64)
get() { grep -h "^$2=" "$1" | head -1 | cut -d= -f2-; }

{
cat <<EOF
# GrooveMorph ${version} (Beta)

**This is a beta prerelease for testers. It is not a stable release.**

## What was and was not checked

- **No DAW validation has been done.** Nothing in this release has been loaded in a real host (VST3, CLAP or otherwise) by a person.
- **No listening validation has been done.** No audio device playback and no listening test of any effect, morph, loop or the demo loop has been performed. Treat sound quality as unknown.
- **No clean-machine install test and no installer.** The install steps in \`INSTALL.md\` have not been run by a person on a clean machine.
- What automation did check, per platform (below): the code compiled, the portable core and processor tests (\`ctest\`) passed on that platform, and the CLAP validator verdict (informational, non-blocking).
- Built from commit \`${commit}\`. Workflow run: ${run_url:-n/a}

## Packages

| Platform | Package | ctest | clap-validator | Signing |
|---|---|---|---|---|
EOF
for p in "${platforms[@]}"; do
  f="$dist/GrooveMorph-${version}-${p}.status.txt"
  if [ -f "$f" ]; then
    echo "| ${p} | \`$(get "$f" zip)\` | $(get "$f" ctest) | $(get "$f" clap_validator) | $(get "$f" signing) |"
  else
    echo "| ${p} | **missing from this release** | n/a | n/a | n/a |"
  fi
done
cat <<EOF

Each zip contains the Standalone app, a VST3 and a CLAP, \`INSTALL.md\`, \`THIRD-PARTY-NOTICES.md\`, \`KNOWN-ISSUES.md\`, \`BUILD-MANIFEST.md\` and the test logs. SHA-256 checksums are in \`SHA256SUMS\` and in a \`.sha256\` file next to each zip.

## Packaging limitations

- Windows binaries are unsigned (SmartScreen will warn). The Microsoft Visual C++ Redistributable (x64) may be required; this was not verified.
- macOS builds are separate arm64 and Intel (x86_64) packages. They are only ad-hoc signed, not Developer ID signed and not notarized; Gatekeeper will block them until quarantine is cleared (see \`INSTALL.md\`).
- The Linux build targets glibc 2.35 or newer (built on Ubuntu 22.04). Only x86_64 is built.
- CLAP support comes from the community adapter clap-juce-extensions pinned at a specific commit. It has never been run in a CLAP host by us. Sample-accurate CLAP event splitting is not enabled.
- Only the Standalone, VST3 and CLAP formats exist. No AU, AAX or LV2.

## Licensing

No project licence has been declared for GrooveMorph. It is built with JUCE 8, which is available under AGPLv3 or a commercial licence; the project owner must choose a licence compatible with one of those before any wider distribution. See \`THIRD-PARTY-NOTICES.md\`.

## Known issues

The text below is copied verbatim from \`docs/alpha/KNOWN-ISSUES.md\` at the built commit. Its heading still says "alpha"; the list has not been rewritten for the beta.

---

EOF
cat "$root/docs/alpha/KNOWN-ISSUES.md"
echo
echo "---"
} > "$output"
echo "wrote $output"
