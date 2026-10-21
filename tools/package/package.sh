#!/usr/bin/env bash
# Package one macOS or Linux GrooveMorph beta build into GrooveMorph-<version>-<platform>.zip.
#
# Usage:
#   package.sh --version 0.2.0-beta.1 --platform macos-arm64|macos-x86_64|linux-x86_64 \
#              --build-dir build --out-dir dist [--commit SHA] [--runner NAME] [--run-url URL] \
#              [--ctest-log FILE] [--validator-dir DIR]
#
# Fails (non-zero) when any expected artefact is missing. Writes into <out-dir>:
#   GrooveMorph-<version>-<platform>.zip
#   GrooveMorph-<version>-<platform>.zip.sha256
#   GrooveMorph-<version>-<platform>.status.txt   (key=value, read by make-release-notes.sh)
set -euo pipefail

version="" platform="" build_dir="" out_dir="" commit="unknown" runner="unknown" run_url=""
ctest_log="" validator_dir=""
while [ $# -gt 0 ]; do
  case "$1" in
    --version) version="$2"; shift 2 ;;
    --platform) platform="$2"; shift 2 ;;
    --build-dir) build_dir="$2"; shift 2 ;;
    --out-dir) out_dir="$2"; shift 2 ;;
    --commit) commit="$2"; shift 2 ;;
    --runner) runner="$2"; shift 2 ;;
    --run-url) run_url="$2"; shift 2 ;;
    --ctest-log) ctest_log="$2"; shift 2 ;;
    --validator-dir) validator_dir="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
for v in version platform build_dir out_dir; do
  [ -n "${!v}" ] || { echo "missing --${v//_/-}" >&2; exit 2; }
done
case "$platform" in
  macos-arm64|macos-x86_64|linux-x86_64) ;;
  *) echo "unsupported platform for package.sh: $platform" >&2; exit 2 ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sha256_of() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'; else shasum -a 256 "$1" | awk '{print $1}'; fi; }

artefacts="$build_dir/GrooveMorphPlugin_artefacts"
[ -d "$artefacts" ] || { echo "no artefacts directory: $artefacts" >&2; exit 1; }
# Single-config generators use <artefacts>/<Config>/..., so search one level down for the named items.
find_one() { # $1 = kind dir (Standalone|VST3|CLAP), $2 = item name
  local hit
  hit="$(find "$artefacts" -mindepth 2 -maxdepth 3 -path "*/$1/$2" -print -quit)"
  [ -n "$hit" ] || { echo "missing artefact: $1/$2 under $artefacts" >&2; exit 1; }
  printf '%s\n' "$hit"
}

if [ "${platform#macos}" != "$platform" ]; then
  standalone_name="GrooveMorph.app"
else
  standalone_name="GrooveMorph"
fi
standalone="$(find_one Standalone "$standalone_name")"
vst3="$(find_one VST3 GrooveMorph.vst3)"
clap="$(find_one CLAP GrooveMorph.clap)"

name="GrooveMorph-${version}-${platform}"
stage="$out_dir/$name"
rm -rf "$stage"
mkdir -p "$stage"

copy_item() { # preserves bundles and symlinks
  if [ "$(uname -s)" = Darwin ]; then ditto "$1" "$2"; else cp -a "$1" "$2"; fi
}
copy_item "$standalone" "$stage/$standalone_name"
copy_item "$vst3" "$stage/GrooveMorph.vst3"
copy_item "$clap" "$stage/GrooveMorph.clap"

arch_note="n/a"
if [ "${platform#macos}" != "$platform" ]; then
  # Ad-hoc sign so arm64 code is runnable. This is NOT Developer ID signing and NOT notarization.
  for bundle in "$stage/$standalone_name" "$stage/GrooveMorph.vst3" "$stage/GrooveMorph.clap"; do
    codesign --force --deep --sign - "$bundle"
  done
  expect="${platform#macos-}"
  arch_note=""
  for bin in "$stage/GrooveMorph.app/Contents/MacOS/GrooveMorph" \
             "$stage/GrooveMorph.vst3/Contents/MacOS/GrooveMorph" \
             "$stage/GrooveMorph.clap/Contents/MacOS/GrooveMorph"; do
    [ -f "$bin" ] || { echo "missing macOS binary: $bin" >&2; exit 1; }
    archs="$(lipo -archs "$bin")"
    [ "$archs" = "$expect" ] || { echo "$bin has architectures '$archs', expected '$expect'" >&2; exit 1; }
    arch_note="$arch_note$(basename "$(dirname "$(dirname "$(dirname "$bin")")")")=$archs "
  done
fi

cp "$repo_root/docs/beta/INSTALL.md" "$stage/INSTALL.md"
cp "$repo_root/THIRD-PARTY-NOTICES.md" "$stage/THIRD-PARTY-NOTICES.md"
cp "$repo_root/docs/alpha/KNOWN-ISSUES.md" "$stage/KNOWN-ISSUES.md"
cp "$repo_root/VERSION" "$stage/VERSION"
[ -f "$repo_root/help/QUICK-START.md" ] && cp "$repo_root/help/QUICK-START.md" "$stage/QUICK-START.md"

ctest_status="passed (see ctest.log)"
if [ -n "$ctest_log" ] && [ -f "$ctest_log" ]; then cp "$ctest_log" "$stage/ctest.log"; else ctest_status="log not provided"; fi

validator_status="not run"
if [ -n "$validator_dir" ] && [ -f "$validator_dir/clap-validator-status.txt" ]; then
  validator_status="$(cat "$validator_dir/clap-validator-status.txt")"
  [ -f "$validator_dir/clap-validator.log" ] && cp "$validator_dir/clap-validator.log" "$stage/clap-validator.log"
fi

standalone_bin="$stage/$standalone_name"
[ "${platform#macos}" != "$platform" ] && standalone_bin="$stage/GrooveMorph.app/Contents/MacOS/GrooveMorph"
vst3_bin="$(find "$stage/GrooveMorph.vst3" -type f \( -name '*.so' -o -path '*/Contents/MacOS/*' \) -print -quit)"
clap_bin="$stage/GrooveMorph.clap"
[ -d "$clap_bin" ] && clap_bin="$stage/GrooveMorph.clap/Contents/MacOS/GrooveMorph"
[ -n "$vst3_bin" ] || { echo "could not locate the VST3 binary inside the bundle" >&2; exit 1; }

signing="none (unsigned)"
[ "${platform#macos}" != "$platform" ] && signing="ad-hoc codesign only; NOT Developer ID signed, NOT notarized"

cat > "$stage/BUILD-MANIFEST.md" <<EOF
# Build manifest

- Version: ${version}
- Platform: ${platform}
- Commit: ${commit}
- Workflow run: ${run_url:-n/a}
- Runner: ${runner}
- Architectures (macOS): ${arch_note}
- Standalone binary SHA-256: $(sha256_of "$standalone_bin")
- VST3 binary SHA-256: $(sha256_of "$vst3_bin")
- CLAP binary SHA-256: $(sha256_of "$clap_bin")
- ctest: ${ctest_status}
- clap-validator: ${validator_status}
- Signing: ${signing}
- NOT performed: DAW hosting of any format, audio-device playback, listening tests, installer, clean-machine install test.
EOF

mkdir -p "$out_dir"
zip_path="$out_dir/$name.zip"
rm -f "$zip_path" "$zip_path.sha256"
if [ "$(uname -s)" = Darwin ]; then
  ditto -c -k --sequesterRsrc --keepParent "$stage" "$zip_path"
else
  (cd "$out_dir" && zip -r -y -q "$name.zip" "$name")
fi
zip_sha="$(sha256_of "$zip_path")"
printf '%s  %s\n' "$zip_sha" "$name.zip" > "$zip_path.sha256"

cat > "$out_dir/$name.status.txt" <<EOF
platform=${platform}
runner=${runner}
ctest=${ctest_status}
clap_validator=${validator_status}
signing=${signing}
zip=${name}.zip
sha256=${zip_sha}
EOF
echo "packaged $zip_path (sha256 $zip_sha)"
