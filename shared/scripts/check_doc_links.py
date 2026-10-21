#!/usr/bin/env python3
"""Check that relative markdown links in the given files/directories resolve.

Usage: check_doc_links.py [--pending NAME ...] PATH [PATH ...]
Links to names listed with --pending (files owned by other workers) are reported
as pending instead of failing. External (scheme:) links and pure anchors are skipped.
"""
import argparse
import pathlib
import re
import sys

LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pending", nargs="*", default=[])
    ap.add_argument("paths", nargs="+")
    args = ap.parse_args()
    files = []
    for p in map(pathlib.Path, args.paths):
        files += sorted(p.rglob("*.md")) if p.is_dir() else [p]
    bad = pending = checked = 0
    for f in files:
        for n, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
            for target in LINK.findall(line):
                if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target) or target.startswith("#"):
                    continue
                path = target.split("#")[0]
                if not path:
                    continue
                checked += 1
                if (f.parent / path).exists():
                    continue
                if pathlib.Path(path).name in args.pending:
                    pending += 1
                    print(f"pending  {f}:{n}: {target}")
                else:
                    bad += 1
                    print(f"BROKEN   {f}:{n}: {target}")
    print(f"checked {checked} links, {bad} broken, {pending} pending")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
