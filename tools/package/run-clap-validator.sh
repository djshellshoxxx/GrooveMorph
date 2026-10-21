#!/usr/bin/env bash
# Download the pinned clap-validator release asset for this platform, verify its SHA-256, and run it
# against a built .clap. NON-BLOCKING by design: this script exits 0 whenever the validator could be
# run, and records the verdict in <out-dir>/clap-validator-status.txt ("passed" or "FAILED (exit N)")
# and the full output in <out-dir>/clap-validator.log. It exits non-zero only when the validator itself
# could not be obtained or started, so a broken download is not mistaken for a plugin verdict.
#
# Usage: run-clap-validator.sh <windows|macos-arm64|macos-x86_64|linux> <path/to/GrooveMorph.clap> <out-dir>
set -uo pipefail

target="${1:?platform}" clap="${2:?clap path}" out_dir="${3:?out dir}"
mkdir -p "$out_dir"
log="$out_dir/clap-validator.log"
status="$out_dir/clap-validator-status.txt"

base="https://github.com/free-audio/clap-validator/releases/download/0.4.1"
prefix="clap-validator-0.4.1-127-g152b982"
case "$target" in
  windows)     asset="$prefix-windows.zip";         sha="d935c3af0a45c3911ea2e900f4aa5d6709dac82bb485f0c4ce28648ab2cd0c10" ;;
  macos-arm64) asset="$prefix-macos-15-aarch64.zip"; sha="719a0248ea431718bb7c92c8a0b9a78afa0bb23d692cf452e80973fe8b89282d" ;;
  macos-x86_64) asset="$prefix-macos-15-x86_64.zip"; sha="c40d28171e581313bc5053e8de887035dcd2942cd733569e80a466cb11a03c25" ;;
  linux)       asset="$prefix-ubuntu-22.04.zip";    sha="49edadcfb407ea0dd946ce418300e853fbd2660fa4b0d00e4f19ff8eef24ad90" ;;
  *) echo "unknown platform $target" >&2; exit 2 ;;
esac

work="$(mktemp -d)"
sha256_of() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'; else shasum -a 256 "$1" | awk '{print $1}'; fi; }

if ! curl --fail --location --silent --show-error --retry 3 -o "$work/v.zip" "$base/$asset"; then
  echo "could not download clap-validator" | tee -a "$log"; echo "not run (download failed)" > "$status"; exit 1
fi
if [ "$(sha256_of "$work/v.zip")" != "$sha" ]; then
  echo "clap-validator checksum mismatch for $asset" | tee -a "$log"; echo "not run (checksum mismatch)" > "$status"; exit 1
fi

# Windows archives hold clap-validator.exe directly; macOS/Linux archives hold a .tar.gz of the binary.
# Git Bash's tar cannot read zip, so Windows uses 7-Zip (preinstalled on GitHub Windows runners).
if [ "$target" = windows ]; then 7z x -y "-o$work" "$work/v.zip" >/dev/null; else unzip -q "$work/v.zip" -d "$work"; fi
if ls "$work"/*.tar.gz >/dev/null 2>&1; then tar -xzf "$work"/*.tar.gz -C "$work"; fi
validator="$(find "$work" -type f \( -name clap-validator -o -name clap-validator.exe \) -print -quit)"
if [ -z "$validator" ]; then echo "clap-validator binary not found in archive" | tee -a "$log"; echo "not run (extract failed)" > "$status"; exit 1; fi
chmod +x "$validator"

runner=("$validator")
if [ "$target" = linux ] && command -v xvfb-run >/dev/null 2>&1; then runner=(xvfb-run -a "$validator"); fi

{ echo "clap-validator $("$validator" --version 2>&1 | head -1)"; echo "asset: $asset ($sha)"; echo "plugin: $clap"; echo; } > "$log"
"${runner[@]}" validate "$clap" >> "$log" 2>&1
code=$?
if [ "$code" -eq 0 ]; then echo "passed (clap-validator 0.4.1)" > "$status"; else echo "FAILED (exit $code; clap-validator 0.4.1; see clap-validator.log)" > "$status"; fi
echo "clap-validator: $(cat "$status")"
tail -n 40 "$log"
exit 0
