#!/usr/bin/env bash
# Validate that the release request agrees with the single version source (the VERSION file).
#
# Usage: check-version.sh <event_name> <ref_name> <input_version>
#   event_name     GitHub event ("push" for a tag push, "workflow_dispatch" for a manual run)
#   ref_name       tag name for tag pushes (e.g. v0.2.0-beta.1); ignored for dispatch
#   input_version  the dispatch "version" input, may be empty
# On success prints version/tag and, when GITHUB_OUTPUT is set, appends version= and tag= to it.
set -euo pipefail

event="${1:?event_name}" ref_name="${2:-}" input_version="${3:-}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
version="$(tr -d '[:space:]' < "$root/VERSION")"

if ! [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+-beta\.[0-9]+$ ]]; then
  echo "::error::VERSION is '$version'; beta-release.yml only publishes MAJOR.MINOR.PATCH-beta.N" >&2
  exit 1
fi
tag="v$version"

case "$event" in
  push)
    if [ "$ref_name" != "$tag" ]; then
      echo "::error::tag '$ref_name' does not match VERSION ('$tag'). Edit VERSION, commit, then tag that commit." >&2
      exit 1
    fi
    ;;
  workflow_dispatch)
    if [ -n "$input_version" ] && [ "${input_version#v}" != "$version" ]; then
      echo "::error::input version '$input_version' does not match VERSION '$version'. VERSION is the single source; edit it and commit first." >&2
      exit 1
    fi
    ;;
  *)
    echo "::error::unsupported event '$event'" >&2
    exit 1
    ;;
esac

echo "version=$version tag=$tag"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
  { echo "version=$version"; echo "tag=$tag"; } >> "$GITHUB_OUTPUT"
fi
